#include "ollama_runtime.h"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

#include "../utils/log.h"

namespace dw {
namespace {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    size_t bytes = size * nmemb;
    buffer->append(ptr, bytes);
    return bytes;
}

std::string curlGet(const std::string& url, long timeoutMs = 1500) {
    CURL* curl = curl_easy_init();
    if (!curl)
        return {};

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200)
        return {};
    return response;
}

} // namespace

OllamaRuntimeStatus OllamaRuntimeStatus::ok(std::string endpoint) {
    OllamaRuntimeStatus status;
    status.ready = true;
    status.endpoint = std::move(endpoint);
    return status;
}

OllamaRuntimeStatus OllamaRuntimeStatus::executableMissing() {
    OllamaRuntimeStatus status;
    status.reason = "Ollama is not installed";
    status.actionCommand = "sudo pacman -S --needed ollama";
    return status;
}

OllamaRuntimeStatus OllamaRuntimeStatus::serverUnavailable() {
    OllamaRuntimeStatus status;
    status.reason = "Ollama runner did not become reachable";
    status.actionCommand = "ollama serve";
    return status;
}

OllamaRuntimeStatus OllamaRuntimeStatus::modelMissing(const std::string& model) {
    OllamaRuntimeStatus status;
    status.reason = "Ollama model is not installed";
    status.actionCommand = ollama::pullCommand(model);
    return status;
}

namespace ollama {

std::string baseUrl(const OllamaRuntimeConfig& config) {
    return "http://" + config.host + ":" + std::to_string(config.port);
}

std::string chatCompletionsEndpoint(const OllamaRuntimeConfig& config) {
    return baseUrl(config) + "/v1/chat/completions";
}

std::string tagsEndpoint(const OllamaRuntimeConfig& config) {
    return baseUrl(config) + "/api/tags";
}

std::string pullCommand(const std::string& model) {
    return "ollama pull " + model;
}

bool tagsResponseHasModel(const std::string& responseJson, const std::string& model) {
    try {
        auto json = nlohmann::json::parse(responseJson);
        if (!json.contains("models") || !json["models"].is_array())
            return false;
        for (const auto& item : json["models"]) {
            if (item.value("name", std::string{}) == model)
                return true;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return false;
}

bool executableExists() {
    return std::system("command -v ollama >/dev/null 2>&1") == 0;
}

} // namespace ollama

OllamaRuntime::OllamaRuntime(OllamaRuntimeConfig config)
    : m_config(std::move(config)) {}

OllamaRuntime::~OllamaRuntime() {
    stopOwnedProcess();
}

void OllamaRuntime::setConfig(OllamaRuntimeConfig config) {
    stopOwnedProcess();
    m_config = std::move(config);
}

std::string OllamaRuntime::endpoint() const {
    return ollama::chatCompletionsEndpoint(m_config);
}

bool OllamaRuntime::ownsProcess() const {
    return m_pid > 0;
}

OllamaRuntimeStatus OllamaRuntime::ensureReady() {
    if (!ollama::executableExists())
        return OllamaRuntimeStatus::executableMissing();

    if (!isReachable()) {
        if (!m_config.manageProcess || !startOwnedProcess())
            return OllamaRuntimeStatus::serverUnavailable();
        if (!waitUntilReachable())
            return OllamaRuntimeStatus::serverUnavailable();
    }

    bool downloadedModel = false;
    auto tags = fetchTags();
    if (!ollama::tagsResponseHasModel(tags, m_config.model)) {
        if (!m_config.autoPullMissingModel)
            return OllamaRuntimeStatus::modelMissing(m_config.model);

        log::infof("Ollama", "Model %s missing; downloading with `%s`",
                   m_config.model.c_str(),
                   ollama::pullCommand(m_config.model).c_str());
        if (!pullModel())
            return OllamaRuntimeStatus::modelMissing(m_config.model);

        downloadedModel = true;
        tags = fetchTags();
    }
    if (!ollama::tagsResponseHasModel(tags, m_config.model))
        return OllamaRuntimeStatus::modelMissing(m_config.model);

    auto status = OllamaRuntimeStatus::ok(endpoint());
    status.modelDownloaded = downloadedModel;
    return status;
}

#ifndef _WIN32
namespace {

// Copy of the environment with `entry` ("NAME=value") replacing any existing
// NAME. Built before fork() so the child never allocates. `entry` must
// outlive the returned vector's use.
std::vector<char*> childEnvWith(const std::string& entry) {
    const auto eq = entry.find('=');
    const size_t nameLen = (eq == std::string::npos ? entry.size() : eq + 1);
    std::vector<char*> env;
    for (char** e = environ; *e != nullptr; ++e) {
        if (std::strncmp(*e, entry.c_str(), nameLen) != 0)
            env.push_back(*e);
    }
    env.push_back(const_cast<char*>(entry.c_str()));
    env.push_back(nullptr);
    return env;
}

} // namespace
#endif

bool OllamaRuntime::startOwnedProcess() {
#ifdef _WIN32
    return false;
#else
    if (m_pid > 0) {
        if (waitpid(m_pid, nullptr, WNOHANG) == 0) {
            // Alive but unreachable (this is only called when probes fail):
            // a wedged child would block restart forever.
            log::warningf("Ollama",
                          "server pid=%d is alive but unreachable; restarting it",
                          m_pid);
            stopOwnedProcess();
        } else {
            // Already exited: reap so the pid does not block a restart.
            m_pid = -1;
        }
    }

    // Built before fork(): allocating (setenv included) between fork and
    // exec can deadlock a multithreaded parent on the malloc lock.
    const std::string hostEnv =
        "OLLAMA_HOST=" + m_config.host + ":" + std::to_string(m_config.port);
    std::vector<char*> childEnv = childEnvWith(hostEnv);
    char* childArgv[] = {const_cast<char*>("ollama"), const_cast<char*>("serve"),
                         nullptr};

    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        environ = childEnv.data();
        execvp("ollama", childArgv);
        _exit(127);
    }

    m_pid = static_cast<int>(pid);
    log::infof("Ollama", "Started app-owned Ollama runner pid=%d endpoint=%s",
               m_pid,
               endpoint().c_str());
    return true;
#endif
}

bool OllamaRuntime::waitUntilReachable() {
    for (int i = 0; i < 40; ++i) {
        if (isReachable())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

bool OllamaRuntime::isReachable() const {
    return !curlGet(ollama::tagsEndpoint(m_config), 500).empty();
}

std::string OllamaRuntime::fetchTags() const {
    return curlGet(ollama::tagsEndpoint(m_config), 3000);
}

bool OllamaRuntime::pullModel() const {
#ifdef _WIN32
    return false;
#else
    // Built before fork() — see startOwnedProcess.
    const std::string hostEnv =
        "OLLAMA_HOST=" + m_config.host + ":" + std::to_string(m_config.port);
    std::vector<char*> childEnv = childEnvWith(hostEnv);
    char* childArgv[] = {const_cast<char*>("ollama"), const_cast<char*>("pull"),
                         const_cast<char*>(m_config.model.c_str()), nullptr};

    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        environ = childEnv.data();
        execvp("ollama", childArgv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid)
        return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log::warningf("Ollama", "Model pull failed for %s", m_config.model.c_str());
        return false;
    }

    log::infof("Ollama", "Model pull complete for %s", m_config.model.c_str());
    return true;
#endif
}

void OllamaRuntime::stopOwnedProcess() {
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

} // namespace dw
