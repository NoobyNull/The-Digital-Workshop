#include "cam_engine_client.h"

#include <nlohmann/json.hpp>

#include "../utils/lmstudio_http.h"

namespace dw::cam {

std::optional<EngineHealth> parseHealth(const std::string& json) {
    const auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;

    EngineHealth health;
    health.ok = parsed.value("ok", false);
    health.service = parsed.value("service", std::string());
    health.version = parsed.value("version", 0);
    return health;
}

std::vector<EngineMachine> parseMachines(const std::string& json) {
    std::vector<EngineMachine> machines;
    const auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return machines;

    for (const auto& entry : parsed) {
        if (!entry.is_object()) continue;
        EngineMachine machine;
        machine.id = entry.value("id", std::string());
        machine.name = entry.value("name", std::string());
        machine.description = entry.value("description", std::string());
        machine.fileExtension = entry.value("fileExtension", std::string());
        machines.push_back(std::move(machine));
    }
    return machines;
}

EngineJobResult parseJobResult(const std::string& json) {
    const auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return EngineJobResult{false, {}, "engine returned unparseable response"};
    }

    EngineJobResult result;
    result.ok = parsed.value("ok", false);
    result.gcode = parsed.value("gcode", std::string());
    result.error = parsed.value("error", std::string());
    return result;
}

CamEngineClient::CamEngineClient(std::string baseUrl) : m_baseUrl(std::move(baseUrl)) {}

std::optional<EngineHealth> CamEngineClient::health() const {
    const auto body = lmstudio::curlGet(m_baseUrl + "/api/health");
    if (body.empty()) return std::nullopt;
    return parseHealth(body);
}

std::vector<EngineMachine> CamEngineClient::machines() const {
    const auto body = lmstudio::curlGet(m_baseUrl + "/api/machines");
    if (body.empty()) return {};
    return parseMachines(body);
}

EngineJobResult CamEngineClient::submitJob(const std::string& jobSpecJson) const {
    const auto body = lmstudio::curlPost(m_baseUrl + "/api/job", jobSpecJson);
    if (body.empty()) return EngineJobResult{false, {}, "engine unreachable"};
    return parseJobResult(body);
}

} // namespace dw::cam
