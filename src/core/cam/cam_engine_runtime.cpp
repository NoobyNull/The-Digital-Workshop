#include "cam_engine_runtime.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

#include "../paths/app_paths.h"
#include "../utils/log.h"
#include "cam_engine_client.h"

namespace dw::cam {

CamEngineStatus CamEngineStatus::ok(std::string endpoint) {
    CamEngineStatus status;
    status.ready = true;
    status.endpoint = std::move(endpoint);
    return status;
}

CamEngineStatus CamEngineStatus::payloadMissing(const Path& dir) {
    CamEngineStatus status;
    status.reason = "CAM engine payload is incomplete at " + dir.string();
    return status;
}

CamEngineStatus CamEngineStatus::engineUnavailable() {
    CamEngineStatus status;
    status.reason = "CAM engine did not become reachable";
    return status;
}

CamEngineStatus CamEngineStatus::unmanagedPlatform() {
    CamEngineStatus status;
    status.reason = "CAM engine process management is not supported on this platform";
    return status;
}

std::string baseUrl(const CamEngineConfig& config) {
    return "http://" + config.host + ":" + std::to_string(config.port);
}

bool payloadLooksComplete(const Path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return false;
    if (!std::filesystem::is_regular_file(dir / "dw-cam-engine.js", ec))
        return false;
    // manifold-3d stays outside the bundle (its emscripten glue breaks when
    // bundled) — the installer requires it, so the runtime check must too.
    if (!std::filesystem::is_directory(dir / "node_modules" / "manifold-3d", ec))
        return false;

    const Path bun = dir / "bun";
    if (!std::filesystem::is_regular_file(bun, ec))
        return false;
#ifdef _WIN32
    return true;
#else
    // access(X_OK) checks *our* execute permission; the owner-exec bit alone
    // is wrong for root-owned 0700 installs and group/other-exec payloads.
    return ::access(bun.c_str(), X_OK) == 0;
#endif
}

Path locatePayloadDir(const Path& exeDir) {
    if (const char* override = std::getenv("DW_CAM_ENGINE_DIR"))
        return Path(override);
    return paths::findBundledResourceDirForExe(exeDir, "cam-engine");
}

uint16_t bridgePortFromEnv(uint16_t fallback) {
    const char* override = std::getenv("DW_BRIDGE_PORT");
    if (override == nullptr || *override == '\0')
        return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(override, &end, 10);
    if (end == override || *end != '\0' || parsed < 1 || parsed > 65535) {
        log::warningf("CamEngine", "ignoring invalid DW_BRIDGE_PORT=%s", override);
        return fallback;
    }
    return static_cast<uint16_t>(parsed);
}

CamEngineRuntime::CamEngineRuntime(CamEngineConfig config)
    : m_config(std::move(config)) {}

CamEngineRuntime::~CamEngineRuntime() {
    stopOwnedProcess();
}

bool CamEngineRuntime::ownsProcess() const {
    return m_pid > 0;
}

bool CamEngineRuntime::ownedChildExited() {
#ifdef _WIN32
    return false;
#else
    if (m_pid <= 0)
        return false;
    if (waitpid(m_pid, nullptr, WNOHANG) == 0)
        return false;
    m_pid = -1;
    return true;
#endif
}

CamEngineStatus CamEngineRuntime::ensureReady() {
    if (isReachable())
        return CamEngineStatus::ok(baseUrl(m_config));

    if (!m_config.manageProcess)
        return CamEngineStatus::engineUnavailable();

#ifdef _WIN32
    // Platform verdict before payload inspection: a POSIX payload check can
    // never pass here, and "payload incomplete" would misdirect diagnosis.
    return CamEngineStatus::unmanagedPlatform();
#else
    if (!payloadLooksComplete(m_config.payloadDir))
        return CamEngineStatus::payloadMissing(m_config.payloadDir);

    if (!startOwnedProcess() || !waitUntilReachable())
        return CamEngineStatus::engineUnavailable();
    return CamEngineStatus::ok(baseUrl(m_config));
#endif
}

bool CamEngineRuntime::startOwnedProcess() {
#ifdef _WIN32
    return false;
#else
    if (m_pid > 0) {
        if (waitpid(m_pid, nullptr, WNOHANG) == 0) {
            // Still alive, yet ensureReady() only reaches here when the
            // engine is unreachable: the child is wedged. Kill and replace
            // it instead of short-circuiting on the hung pid forever.
            log::warningf("CamEngine",
                       "engine pid=%d is alive but unreachable; restarting it",
                       m_pid);
            stopOwnedProcess();
        } else {
            // Already exited: reap so the pid does not block a restart.
            m_pid = -1;
        }
    }

    // Everything the child needs is built before fork(): any allocation
    // (including setenv) between fork and exec can deadlock a multithreaded
    // parent on the malloc lock.
    const std::string portEnv = "DW_BRIDGE_PORT=" + std::to_string(m_config.port);
    std::vector<char*> childEnv;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        if (std::strncmp(*entry, "DW_BRIDGE_PORT=", 15) != 0)
            childEnv.push_back(*entry);
    }
    childEnv.push_back(const_cast<char*>(portEnv.c_str()));
    childEnv.push_back(nullptr);
    char* childArgv[] = {const_cast<char*>("bun"),
                         const_cast<char*>("dw-cam-engine.js"),
                         nullptr};

    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        if (chdir(m_config.payloadDir.c_str()) != 0)
            _exit(127);
        execve("./bun", childArgv, childEnv.data());
        _exit(127);
    }

    m_pid = static_cast<int>(pid);
    log::infof("CamEngine", "Started app-owned CAM engine pid=%d endpoint=%s",
               m_pid,
               baseUrl(m_config).c_str());
    return true;
#endif
}

bool CamEngineRuntime::waitUntilReachable() {
    for (int i = 0; i < 40; ++i) {
        if (isReachable())
            return true;
#ifndef _WIN32
        // A dead-on-arrival child (exec failure, port conflict, broken
        // payload) should fail fast, not burn the whole probe budget.
        if (m_pid > 0 && waitpid(m_pid, nullptr, WNOHANG) != 0) {
            log::errorf("CamEngine",
                        "engine pid=%d exited before becoming reachable", m_pid);
            m_pid = -1;
            return false;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

bool CamEngineRuntime::isReachable() const {
    // Quiet 1s probe: connection-refused while the engine boots is the
    // expected case, not an error worth logging 40 times, and a 5s timeout
    // per probe would stretch the wait loop far past its ~10s budget.
    const auto health = CamEngineClient(baseUrl(m_config)).health(1, true);
    return health.has_value() && health->service == "dw-bridge";
}

void CamEngineRuntime::stopOwnedProcess() {
#ifndef _WIN32
    if (m_pid <= 0)
        return;

    int pid = m_pid;
    m_pid = -1;
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    kill(pid, SIGKILL);
    (void)waitpid(pid, nullptr, 0);
#endif
}

} // namespace dw::cam
