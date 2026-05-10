#include "lmstudio_material_service.h"

#include <cctype>
#include <cstring>

#include <nlohmann/json.hpp>

#include "../paths/app_paths.h"
#include "../utils/lmstudio_http.h"
#include "../utils/log.h"
#include "material_archive.h"

namespace dw {

namespace {
const char* kLMStudioModel = "local-model";

std::string stripJsonFence(std::string value) {
    constexpr const char* kFence = "```";
    auto start = value.find(kFence);
    if (start == std::string::npos)
        return value;

    start += std::strlen(kFence);
    if (value.compare(start, 4, "json") == 0)
        start += 4;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;

    auto end = value.rfind(kFence);
    if (end == std::string::npos || end <= start)
        end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(start, end - start);
}
} // anonymous namespace

std::string LMStudioMaterialService::fetchProperties(const std::string& prompt,
                                                      const std::string& endpoint) {
    std::string systemPrompt =
        "You are a material scientist and CNC fabrication specialist. Analyze the material "
        "name provided and return a comprehensive technical profile. Categorize the material "
        "into Wood, Metal, Plastic, Composite, Foam, or Other. Include CNC parameters like "
        "feed rate in mm/min, spindle speed in RPM, and depth per pass in mm. Always interpret "
        "ambiguous names such as Cherry, Bass, Zebra, and Canary as wood timber. Return only "
        "valid JSON with these keys: category, description, density, hardness, colorHex, "
        "recommendedFeedRate, recommendedSpindleSpeed, recommendedDepthPerPass, "
        "recommendedToolType.";

    nlohmann::json requestBody;
    requestBody["model"] = kLMStudioModel;
    requestBody["temperature"] = 0.2;
    requestBody["messages"] = nlohmann::json::array(
        {{{"role", "system"}, {"content", systemPrompt}},
         {{"role", "user"},
          {"content",
           "Analyze the material: \"" + prompt +
               "\". Provide technical physical properties and CNC machining parameters."}}});

    std::string response = lmstudio::curlPost(endpoint, requestBody.dump());
    if (response.empty()) {
        return {};
    }

    // Extract the text content from LM Studio response
    try {
        auto json = nlohmann::json::parse(response);
        auto& choices = json["choices"];
        if (choices.empty()) {
            log::error("LMStudioService", "No choices in properties response");
            return {};
        }
        return stripJsonFence(choices[0]["message"].value("content", std::string{}));
    } catch (const nlohmann::json::exception& e) {
        log::errorf("LMStudioService", "Failed to parse properties response: %s", e.what());
        return {};
    }
}

// ---------------------------------------------------------------------------
// Property parsing + category mapping
// ---------------------------------------------------------------------------

MaterialRecord LMStudioMaterialService::parseProperties(const std::string& json,
                                                      const std::string& name) {
    MaterialRecord record;
    record.name = name;

    try {
        auto props = nlohmann::json::parse(json);

        // Map category string to MaterialCategory enum
        std::string category = props.value("category", "Other");
        if (category == "Wood") {
            record.category = MaterialCategory::Hardwood;
        } else {
            record.category = MaterialCategory::Composite;
        }

        // Parse hardness string (e.g. "1290 lbf") -> float
        std::string hardnessStr = props.value("hardness", "0");
        try {
            record.jankaHardness = std::stof(hardnessStr);
        } catch (...) {
            record.jankaHardness = 0.0f;
        }

        // Convert mm/min -> in/min (divide by 25.4)
        float feedRateMm = props.value("recommendedFeedRate", 0.0f);
        record.feedRate = feedRateMm / 25.4f;

        // RPM direct
        record.spindleSpeed = props.value("recommendedSpindleSpeed", 0.0f);

        // Convert mm -> in (divide by 25.4)
        float depthMm = props.value("recommendedDepthPerPass", 0.0f);
        record.depthOfCut = depthMm / 25.4f;

    } catch (const nlohmann::json::exception& e) {
        log::errorf("LMStudioService", "Failed to parse material properties: %s", e.what());
    }

    return record;
}

// ---------------------------------------------------------------------------
// Main generate flow
// ---------------------------------------------------------------------------

GenerateResult LMStudioMaterialService::generate(const std::string& prompt,
                                                 const std::string& endpoint) {
    GenerateResult result;

    log::infof("LMStudioService", "Generating material: %s", prompt.c_str());

    std::string propsJson = fetchProperties(prompt, endpoint);
    if (propsJson.empty()) {
        result.error = "Failed to fetch material properties from LM Studio";
        return result;
    }

    // Parse properties into MaterialRecord
    result.record = parseProperties(propsJson, prompt);

    // Create .dwmat archive in materials directory
    Path materialsDir = paths::getMaterialsDir();
    Path archivePath = materialsDir / (prompt + ".dwmat");

    auto archiveResult =
        MaterialArchive::create(archivePath.string(), "", result.record);
    if (!archiveResult.success) {
        result.error = "Failed to create .dwmat archive: " + archiveResult.error;
        return result;
    }

    result.dwmatPath = archivePath;
    result.record.archivePath = archivePath;
    result.success = true;

    log::infof("LMStudioService", "Generated material archive: %s", archivePath.string().c_str());
    return result;
}

} // namespace dw
