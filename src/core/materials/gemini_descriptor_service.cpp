#include "gemini_descriptor_service.h"

#include <fstream>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h> // NOLINT

#include "../utils/gemini_http.h"
#include "../utils/log.h"

namespace dw {

namespace {
const char* kGeminiApiBase = "https://generativelanguage.googleapis.com/v1beta/models/";

struct PngWriteContext {
    std::vector<uint8_t> data;
};

void pngWriteCallback(void* context, void* data, int size) {
    auto* ctx = static_cast<PngWriteContext*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    ctx->data.insert(ctx->data.end(), bytes, bytes + size);
}

} // anonymous namespace

std::vector<uint8_t> GeminiDescriptorService::tgaToPng(const std::string& tgaPath) {
    // Read TGA manually (18-byte header, BGRA top-down, 32bpp) — same format as
    // ThumbnailGenerator output, avoiding stb_image dependency here.
    std::ifstream file(tgaPath, std::ios::binary);
    if (!file) {
        log::errorf("Descriptor", "Failed to open TGA: %s", tgaPath.c_str());
        return {};
    }

    uint8_t header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file || header[2] != 2 || header[16] != 32) {
        return {};
    }

    int width = header[12] | (header[13] << 8);
    int height = header[14] | (header[15] << 8);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return {};
    }

    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> bgra(pixelCount * 4);
    file.read(reinterpret_cast<char*>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
    if (!file) {
        return {};
    }

    // Convert BGRA -> RGB for PNG
    std::vector<uint8_t> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = bgra[i * 4 + 2];
        rgb[i * 3 + 1] = bgra[i * 4 + 1];
        rgb[i * 3 + 2] = bgra[i * 4 + 0];
    }

    PngWriteContext writeCtx;
    int ok = stbi_write_png_to_func(
        pngWriteCallback, &writeCtx, width, height, 3, rgb.data(), width * 3);
    if (!ok) {
        log::error("Descriptor", "Failed to encode PNG");
        return {};
    }

    return writeCtx.data;
}

std::string GeminiDescriptorService::fetchClassification(const std::vector<uint8_t>& imageData,
                                                         const std::string& apiKey) {
    std::string url = std::string(kGeminiApiBase) +
                      "gemini-2.5-flash:generateContent?key=" + apiKey;

    std::string base64Image = gemini::base64Encode(imageData);

    // Structured response schema for reliable JSON output
    nlohmann::json schema;
    schema["type"] = "OBJECT";
    schema["properties"] = {{"title", {{"type", "STRING"}}},
                            {"description", {{"type", "STRING"}}},
                            {"hoverNarrative", {{"type", "STRING"}}},
                            {"keywords", {{"type", "ARRAY"}, {"items", {{"type", "STRING"}}}}},
                            {"associations", {{"type", "ARRAY"}, {"items", {{"type", "STRING"}}}}},
                            {"categories", {{"type", "ARRAY"}, {"items", {{"type", "STRING"}}}}}};
    schema["required"] =
        nlohmann::json::array({"title", "description", "hoverNarrative", "keywords", "categories"});

    nlohmann::json requestBody;
    requestBody["systemInstruction"]["parts"] = nlohmann::json::array(
        {{{"text",
           "You are The Descriptor — an art historian and design taxonomist. "
           "Analyze the depicted SUBJECT MATTER of the 3D model shown in the thumbnail, "
           "ignoring the physical medium (it is always a 3D model). "
           "Focus on WHAT is depicted, not HOW it is rendered. "
           "Provide:\n"
           "- title: A concise name for the depicted object (max 60 chars)\n"
           "- description: 2-3 sentence description of the subject, style, and design intent\n"
           "- hoverNarrative: A single evocative sentence for tooltip display (max 120 chars)\n"
           "- keywords: 3-5 descriptive tags\n"
           "- associations: Any recognizable brands, logos, or cultural references "
           "(empty array if none)\n"
           "- categories: A classification chain from broad to specific (2-4 levels). "
           "Each entry must be ONE concept only — never combine with 'and', '&', or '/'. "
           "Example: [\"Art\", \"Sculpture\", \"Figurine\"] not [\"Art & Sculpture\"]"}}});

    requestBody["contents"] = nlohmann::json::array(
        {{{"parts",
           nlohmann::json::array(
               {{{"text", "Classify this 3D model thumbnail."}},
                {{"inlineData", {{"mimeType", "image/png"}, {"data", base64Image}}}}})}}});

    requestBody["generationConfig"]["responseMimeType"] = "application/json";
    requestBody["generationConfig"]["responseSchema"] = schema;

    std::string response = gemini::curlPost(url, requestBody.dump());
    if (response.empty()) {
        return {};
    }

    try {
        auto json = nlohmann::json::parse(response);
        auto& candidates = json["candidates"];
        if (candidates.empty()) {
            log::error("Descriptor", "No candidates in response");
            return {};
        }
        auto& parts = candidates[0]["content"]["parts"];
        if (parts.empty()) {
            log::error("Descriptor", "No parts in response");
            return {};
        }
        return parts[0]["text"].get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        log::errorf("Descriptor", "Failed to parse response: %s", e.what());
        return {};
    }
}

DescriptorResult GeminiDescriptorService::parseClassification(const std::string& json) {
    DescriptorResult result;

    try {
        auto response = nlohmann::json::parse(json);
        result.title = response.value("title", "");
        result.description = response.value("description", "");
        result.hoverNarrative = response.value("hoverNarrative", "");

        if (result.title.empty() || result.description.empty()) {
            result.error = "Missing title or description in Gemini response";
            return result;
        }

        // Parse keywords array
        if (response.contains("keywords") && response["keywords"].is_array()) {
            for (const auto& kw : response["keywords"]) {
                result.keywords.push_back(kw.get<std::string>());
            }
        }

        // Parse associations array
        if (response.contains("associations") && response["associations"].is_array()) {
            for (const auto& assoc : response["associations"]) {
                result.associations.push_back(assoc.get<std::string>());
            }
        }

        // Parse categories array
        if (response.contains("categories") && response["categories"].is_array()) {
            for (const auto& cat : response["categories"]) {
                result.categories.push_back(cat.get<std::string>());
            }
        }

        result.success = true;
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("Failed to parse classification: ") + e.what();
    }

    return result;
}

DescriptorResult GeminiDescriptorService::describe(const std::string& modelFilePath,
                                                   const std::string& apiKey) {
    DescriptorResult result;

    log::infof("DescriptorService", "Describing model: %s", modelFilePath.c_str());

    // Convert TGA thumbnail to PNG
    std::vector<uint8_t> pngData = tgaToPng(modelFilePath);
    if (pngData.empty()) {
        result.error = "Failed to convert model thumbnail to PNG";
        return result;
    }

    // Fetch classification from Gemini
    std::string classificationJson = fetchClassification(pngData, apiKey);
    if (classificationJson.empty()) {
        result.error = "Failed to fetch classification from Gemini API";
        return result;
    }

    // Parse classification response
    result = parseClassification(classificationJson);

    if (result.success) {
        log::infof("DescriptorService", "Successfully described model: %s", result.title.c_str());
    }

    return result;
}

} // namespace dw
