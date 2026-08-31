#include "cam_engine_client.h"

#include <nlohmann/json.hpp>

#include "../utils/lmstudio_http.h"

namespace {

std::string stringOr(const nlohmann::json& j, const char* key, std::string fallback = {}) {
    const auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    return fallback;
}

bool boolOr(const nlohmann::json& j, const char* key, bool fallback = false) {
    const auto it = j.find(key);
    if (it != j.end() && it->is_boolean())
        return it->get<bool>();
    return fallback;
}

int intOr(const nlohmann::json& j, const char* key, int fallback = 0) {
    const auto it = j.find(key);
    if (it != j.end() && it->is_number_integer())
        return it->get<int>();
    return fallback;
}

} // namespace

namespace dw::cam {

std::optional<EngineHealth> parseHealth(const std::string& json) {
    const auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;

    EngineHealth health;
    health.ok = boolOr(parsed, "ok", false);
    health.service = stringOr(parsed, "service");
    health.version = intOr(parsed, "version", 0);
    return health;
}

std::vector<EngineMachine> parseMachines(const std::string& json) {
    std::vector<EngineMachine> machines;
    const auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return machines;

    for (const auto& entry : parsed) {
        if (!entry.is_object()) continue;
        EngineMachine machine;
        machine.id = stringOr(entry, "id");
        machine.name = stringOr(entry, "name");
        machine.description = stringOr(entry, "description");
        machine.fileExtension = stringOr(entry, "fileExtension");
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
    result.ok = boolOr(parsed, "ok", false);
    result.gcode = stringOr(parsed, "gcode");
    result.error = stringOr(parsed, "error");
    return result;
}

CamEngineClient::CamEngineClient(std::string baseUrl) : m_baseUrl(std::move(baseUrl)) {}

std::optional<EngineHealth> CamEngineClient::health(long timeoutSeconds, bool quiet) const {
    const auto body = lmstudio::curlGet(m_baseUrl + "/api/health", timeoutSeconds, quiet);
    if (body.empty()) return std::nullopt;
    return parseHealth(body);
}

std::vector<EngineMachine> CamEngineClient::machines() const {
    const auto body = lmstudio::curlGet(m_baseUrl + "/api/machines");
    if (body.empty()) return {};
    return parseMachines(body);
}

EngineJobResult CamEngineClient::submitJob(const std::string& jobSpecJson) const {
    // Heavy jobs (large meshes, fine finishing passes) legitimately run for
    // minutes; a short timeout here gets misread as a dead engine.
    const auto body = lmstudio::curlPost(m_baseUrl + "/api/job", jobSpecJson, 600);
    if (body.empty())
        return EngineJobResult{false, {}, "engine request failed (unreachable or timed out)"};
    return parseJobResult(body);
}

} // namespace dw::cam
