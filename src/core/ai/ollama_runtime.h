#pragma once

#include <cstdint>
#include <string>

namespace dw {

struct OllamaRuntimeConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 42319;
    std::string model = "llava:latest";
    bool manageProcess = true;
    bool autoPullMissingModel = true;
};

struct OllamaRuntimeStatus {
    bool ready = false;
    bool modelDownloaded = false;
    std::string reason;
    std::string actionCommand;
    std::string endpoint;

    static OllamaRuntimeStatus ok(std::string endpoint);
    static OllamaRuntimeStatus executableMissing();
    static OllamaRuntimeStatus serverUnavailable();
    static OllamaRuntimeStatus modelMissing(const std::string& model);
};

namespace ollama {
std::string baseUrl(const OllamaRuntimeConfig& config);
std::string chatCompletionsEndpoint(const OllamaRuntimeConfig& config);
std::string tagsEndpoint(const OllamaRuntimeConfig& config);
std::string pullCommand(const std::string& model);
bool tagsResponseHasModel(const std::string& responseJson, const std::string& model);
bool executableExists();
} // namespace ollama

class OllamaRuntime {
  public:
    explicit OllamaRuntime(OllamaRuntimeConfig config = {});
    ~OllamaRuntime();

    OllamaRuntime(const OllamaRuntime&) = delete;
    OllamaRuntime& operator=(const OllamaRuntime&) = delete;

    OllamaRuntimeStatus ensureReady();
    void stopOwnedProcess();
    [[nodiscard]] bool ownsProcess() const;
    [[nodiscard]] std::string endpoint() const;
    [[nodiscard]] const OllamaRuntimeConfig& config() const { return m_config; }
    void setConfig(OllamaRuntimeConfig config);

  private:
    bool startOwnedProcess();
    bool waitUntilReachable();
    bool isReachable() const;
    std::string fetchTags() const;
    bool pullModel() const;

    OllamaRuntimeConfig m_config;
    int m_pid = -1;
};

} // namespace dw
