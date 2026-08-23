#include "cam_engine_runtime.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
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

    const Path bun = dir / "bun";
    auto status = std::filesystem::status(bun, ec);
    if (ec || !std::filesystem::is_regular_file(status))
        return false;
    return (status.permissions() & std::filesystem::perms::owner_exec) !=
           std::filesystem::perms::none;
}

Path locatePayloadDir(const Path& exeDir) {
    if (const char* override = std::getenv("DW_CAM_ENGINE_DIR"))
        return Path(override);
    return paths::findBundledResourceDirForExe(exeDir, "cam-engine");
}

CamEngineRuntime::CamEngineRuntime(CamEngineConfig config)
    : m_config(std::move(config)) {}

CamEngineRuntime::~CamEngineRuntime() {
    stopOwnedProcess();
}

bool CamEngineRuntime::ownsProcess() const {
    return m_pid > 0;
}

CamEngineStatus CamEngineRuntime::ensureReady() {
    if (isReachable())
        return CamEngineStatus::ok(baseUrl(m_config));

    if (!m_config.manageProcess)
        return CamEngineStatus::engineUnavailable();
    if (!payloadLooksComplete(m_config.payloadDir))
        return CamEngineStatus::payloadMissing(m_config.payloadDir);

#ifdef _WIN32
    return CamEngineStatus::unmanagedPlatform();
#else
    if (!startOwnedProcess() || !waitUntilReachable())
        return CamEngineStatus::engineUnavailable();
    return CamEngineStatus::ok(baseUrl(m_config));
#endif
}

bool CamEngineRuntime::startOwnedProcess() {
#ifdef _WIN32
    return false;
#else
    if (m_pid > 0)
        return true;

    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        if (chdir(m_config.payloadDir.c_str()) != 0)
            _exit(127);
        setenv("DW_BRIDGE_PORT", std::to_string(m_config.port).c_str(), 1);
        execl("./bun", "bun", "dw-cam-engine.js", static_cast<char*>(nullptr));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

bool CamEngineRuntime::isReachable() const {
    const auto health = CamEngineClient(baseUrl(m_config)).health();
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
