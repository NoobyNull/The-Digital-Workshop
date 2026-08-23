#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dw::cam {

struct EngineHealth {
    bool ok = false;
    std::string service;
    int version = 0;
};

struct EngineMachine {
    std::string id;
    std::string name;
    std::string description;
    std::string fileExtension;
};

struct EngineJobResult {
    bool ok = false;
    std::string gcode;
    std::string error;
};

// Pure parsers — unit-testable without a live engine.
[[nodiscard]] std::optional<EngineHealth> parseHealth(const std::string& json);
[[nodiscard]] std::vector<EngineMachine> parseMachines(const std::string& json);
[[nodiscard]] EngineJobResult parseJobResult(const std::string& json);

// Thin transport over the local sidecar (loopback HTTP, libcurl).
// Covered end-to-end by packaging/smoke-cam-engine.sh and the runtime's
// reachability checks rather than socket-stub unit tests.
class CamEngineClient {
  public:
    explicit CamEngineClient(std::string baseUrl);

    [[nodiscard]] std::optional<EngineHealth> health() const;
    [[nodiscard]] std::vector<EngineMachine> machines() const;
    [[nodiscard]] EngineJobResult submitJob(const std::string& jobSpecJson) const;
    [[nodiscard]] const std::string& baseUrl() const noexcept { return m_baseUrl; }

  private:
    std::string m_baseUrl;
};

} // namespace dw::cam
