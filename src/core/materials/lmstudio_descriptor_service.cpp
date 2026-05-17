#include "lmstudio_descriptor_service.h"

#include <cctype>
#include <cstring>
#include <fstream>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h> // NOLINT

#include "../import/smart_tagger.h"
#include "../utils/lmstudio_http.h"
#include "../utils/log.h"

#include <algorithm>

namespace dw {

namespace {
const char* kDefaultLocalModel = "local-model";

struct PngWriteContext {
    std::vector<uint8_t> data;
};

void pngWriteCallback(void* context, void* data, int size) {
    auto* ctx = static_cast<PngWriteContext*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    ctx->data.insert(ctx->data.end(), bytes, bytes + size);
}

TagClassificationStatus statusFromString(const std::string& value) {
    if (value == "retry_view") return TagClassificationStatus::RetryView;
    if (value == "fallback_isometric") return TagClassificationStatus::FallbackIsometric;
    if (value == "unclassifiable") return TagClassificationStatus::Unclassifiable;
    return TagClassificationStatus::FinalTag;
}

const char* statusName(TagClassificationStatus status) {
    switch (status) {
    case TagClassificationStatus::FinalTag: return "final_tag";
    case TagClassificationStatus::RetryView: return "retry_view";
    case TagClassificationStatus::FallbackIsometric: return "fallback_isometric";
    case TagClassificationStatus::Unclassifiable: return "unclassifiable";
    }
    return "final_tag";
}

GeometryType geometryTypeFromString(const std::string& value) {
    if (value == "true_3d") return GeometryType::True3D;
    if (value == "2.5d_relief") return GeometryType::Relief25D;
    if (value == "flat_part") return GeometryType::FlatPart;
    return GeometryType::Unknown;
}

int sanitizeRotateDegrees(int degrees) {
    if (degrees == 90 || degrees == 180 || degrees == 270)
        return degrees;
    return 0;
}

std::string trim(std::string value) {
    auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
                    return std::isspace(c) != 0;
                }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

bool isCompoundCategory(const std::string& value) {
    return value.find('&') != std::string::npos || value.find('/') != std::string::npos ||
           value.find(" and ") != std::string::npos || value.find(" And ") != std::string::npos;
}

std::vector<std::string> sanitizeCategoryChain(const nlohmann::json& categories) {
    constexpr size_t kMaxAutoCategoryDepth = 4;
    std::vector<std::string> clean;
    if (!categories.is_array())
        return clean;

    for (const auto& cat : categories) {
        if (!cat.is_string())
            continue;
        std::string value = trim(cat.get<std::string>());
        if (value.empty() || isCompoundCategory(value))
            continue;
        if (std::find(clean.begin(), clean.end(), value) != clean.end())
            continue;
        clean.push_back(std::move(value));
        if (clean.size() >= kMaxAutoCategoryDepth)
            break;
    }
    return clean;
}

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

std::vector<uint8_t> LMStudioDescriptorService::tgaToPng(const std::string& tgaPath) {
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

std::string LMStudioDescriptorService::buildClassificationRequestForTest(
    const std::vector<uint8_t>& imageData,
    const std::string& model,
    ThumbnailView currentView) const {
    std::string base64Image = lmstudio::base64Encode(imageData);

    std::string viewText = std::string("Classify this 3D model thumbnail. Current view: ") +
                           smart_tagging::thumbnailViewName(currentView) +
                           ". Available retag views: front, back, left, right, top, bottom. "
                           "Return only JSON matching the provided schema.";

    nlohmann::json requestBody;
    requestBody["model"] = model.empty() ? kDefaultLocalModel : model;
    requestBody["temperature"] = 0.2;
    requestBody["response_format"] = {
        {"type", "json_schema"},
        {"json_schema",
         {{"name", "dw_model_classification"},
          {"schema", nlohmann::json::parse(classificationJsonSchema())}}}};
    requestBody["messages"] = nlohmann::json::array(
        {{{"role", "system"}, {"content", classificationSystemPrompt()}},
         {{"role", "user"},
          {"content",
           nlohmann::json::array(
               {{{"type", "text"}, {"text", viewText}},
                {{"type", "image_url"},
                 {"image_url",
                  {{"url", "data:image/png;base64," + base64Image}}}}})}}});
    return requestBody.dump();
}

std::string LMStudioDescriptorService::fetchClassification(const std::vector<uint8_t>& imageData,
                                                           const std::string& endpoint,
                                                           const std::string& model,
                                                           ThumbnailView currentView) {
    std::string requestBody =
        buildClassificationRequestForTest(imageData, model, currentView);

    std::string response = lmstudio::curlPost(endpoint, requestBody);
    if (response.empty()) {
        return {};
    }

    return extractClassificationJson(response);
}

std::string LMStudioDescriptorService::classificationSystemPrompt() const {
    return "You are classifying a thumbnail of a 3D/CNC model for a local model library. "
           "Identify what the model depicts, not just its geometry or render color. Prefer "
           "practical CNC/library organization terms over broad labels. Return only JSON "
           "matching the provided schema. Use status final_tag only when the subject is "
           "specific and useful. Use retry_view when this view hides key identifying features "
           "and another orthographic view may help. Use fallback_isometric when an "
           "orthographic retry is unlikely to help but an isometric view may help. Use "
           "unclassifiable when the model is too generic, ambiguous, blank, broken, or "
           "visually unidentifiable. For final_tag, title and description must be non-empty. "
           "For retry_view, fallback_isometric, or unclassifiable, title and description may "
           "be empty. Categories must describe what a user would search for in this model "
           "library, not the file type or manufacturing workflow. Never use category names "
           "such as CNC, 3D, 3D Print, 3D Model, STL, Model, Model Library, printable, carving, "
           "routing, or generic art/model buckets. Prefer stable primary roots such as "
           "Animals, Architecture, Art, Decor, Fantasy, Functional, Furniture, Jewelry, "
           "Kitchen, Military, Nature, People, Pop Culture, Religion, Signs, Symbols, or "
           "Vehicles. Categories must be one to four simple single-concept levels, broad to "
           "specific. Do not use compound category names, slash-separated names, or catch-all "
           "category chains. Keywords should contain useful search terms. Associations should "
           "only include recognizable brands, logos, franchises, symbols, or named cultural "
           "references visible in the model. Also evaluate upright orientation from the image. "
           "Set orientation.needsRotation true only when a visible natural upright direction is "
           "clear from faces, text, emblems, signs, figures, animals, vehicles, or similar "
           "directional subjects. Suggest only 0, 90, 180, or 270 degrees clockwise from the "
           "current thumbnail. Judge upright orientation as a browsing thumbnail, not as a "
           "camera angle. Human figures should have heads above torsos, animals should not "
           "appear sideways when a natural standing/swimming/flying direction is visible, text "
           "and emblems should read upright, and the ground or base should appear below the "
           "subject when visible. If people, flags, signs, buildings, vehicles, animals, or "
           "recognizable scenes appear sideways or upside down, set needsRotation true and "
           "choose the nearest 90-degree correction. For abstract, symmetric, or ambiguous "
           "models, set needsRotation false and rotateDegrees 0.";
}

std::string LMStudioDescriptorService::classificationJsonSchema() const {
    return R"({
  "type": "object",
  "properties": {
    "status": {
      "type": "string",
      "enum": ["final_tag", "retry_view", "fallback_isometric", "unclassifiable"]
    },
    "confidence": {
      "type": "number"
    },
    "geometryType": {
      "type": "string",
      "enum": ["true_3d", "2.5d_relief", "flat_part", "unknown"]
    },
    "needsRetag": {
      "type": "boolean"
    },
    "recommendedView": {
      "type": "string",
      "enum": ["front", "back", "left", "right", "top", "bottom", "unknown"]
    },
    "viewReason": {
      "type": "string"
    },
    "title": {
      "type": "string"
    },
    "description": {
      "type": "string"
    },
    "hoverNarrative": {
      "type": "string"
    },
    "keywords": {
      "type": "array",
      "items": { "type": "string" }
    },
    "associations": {
      "type": "array",
      "items": { "type": "string" }
    },
    "categories": {
      "type": "array",
      "items": { "type": "string" },
      "minItems": 1,
      "maxItems": 4
    },
    "orientation": {
      "type": "object",
      "properties": {
        "needsRotation": {
          "type": "boolean"
        },
        "uprightView": {
          "type": "string",
          "enum": ["front", "back", "left", "right", "top", "bottom", "unknown"]
        },
        "rotateDegrees": {
          "type": "integer",
          "enum": [0, 90, 180, 270]
        },
        "reason": {
          "type": "string"
        }
      },
      "required": ["needsRotation", "uprightView", "rotateDegrees", "reason"]
    }
  },
  "required": [
    "status",
    "confidence",
    "geometryType",
    "needsRetag",
    "recommendedView",
    "viewReason",
    "title",
    "description",
    "hoverNarrative",
    "keywords",
    "associations",
    "categories",
    "orientation"
  ]
})";
}

std::string LMStudioDescriptorService::extractClassificationJson(const std::string& responseJson) {
    try {
        auto json = nlohmann::json::parse(responseJson);
        auto& choices = json["choices"];
        if (choices.empty()) {
            log::error("Descriptor", "No choices in LM Studio response");
            return {};
        }
        const auto& message = choices[0]["message"];
        auto content = message.value("content", std::string{});
        if (content.empty()) {
            content = message.value("reasoning_content", std::string{});
        }
        if (content.empty()) {
            log::error("Descriptor", "No message content in LM Studio response");
            return {};
        }
        return stripJsonFence(content);
    } catch (const nlohmann::json::exception& e) {
        log::errorf("Descriptor", "Failed to parse response: %s", e.what());
        return {};
    }
}

DescriptorResult LMStudioDescriptorService::parseClassification(const std::string& json) {
    DescriptorResult result;

    try {
        auto response = nlohmann::json::parse(json);
        result.status = statusFromString(response.value("status", "final_tag"));
        result.confidence =
            std::clamp(static_cast<float>(response.value("confidence", 0.0)), 0.0f, 1.0f);
        result.geometryType = geometryTypeFromString(response.value("geometryType", "unknown"));
        result.needsRetag = response.value("needsRetag", false);
        result.recommendedView =
            smart_tagging::thumbnailViewFromString(response.value("recommendedView", "unknown"));
        result.viewReason = response.value("viewReason", "");
        result.title = response.value("title", "");
        result.description = response.value("description", "");
        result.hoverNarrative = response.value("hoverNarrative", "");

        if (result.status == TagClassificationStatus::FinalTag &&
            (result.title.empty() || result.description.empty())) {
            result.error = "Missing title or description in LM Studio response";
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
            result.categories = sanitizeCategoryChain(response["categories"]);
        }

        if (response.contains("orientation") && response["orientation"].is_object()) {
            const auto& orientation = response["orientation"];
            result.orientation.needsRotation = orientation.value("needsRotation", false);
            result.orientation.uprightView = smart_tagging::thumbnailViewFromString(
                orientation.value("uprightView", "unknown"));
            result.orientation.rotateDegrees =
                sanitizeRotateDegrees(orientation.value("rotateDegrees", 0));
            result.orientation.reason = orientation.value("reason", "");
            if (!result.orientation.needsRotation)
                result.orientation.rotateDegrees = 0;
        }

        result.success = true;
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("Failed to parse classification: ") + e.what();
    }

    return result;
}

DescriptorResult LMStudioDescriptorService::describe(const std::string& modelFilePath,
                                                     const std::string& endpoint,
                                                     const std::string& model,
                                                     ThumbnailView currentView) {
    DescriptorResult result;

    log::infof("DescriptorService", "Describing model: %s", modelFilePath.c_str());

    // Convert TGA thumbnail to PNG
    std::vector<uint8_t> pngData = tgaToPng(modelFilePath);
    if (pngData.empty()) {
        result.error = "Failed to convert model thumbnail to PNG";
        return result;
    }

    // Fetch classification from LM Studio
    std::string classificationJson = fetchClassification(pngData, endpoint, model, currentView);
    if (classificationJson.empty()) {
        result.error = "Failed to fetch classification from LM Studio";
        return result;
    }

    // Parse classification response
    result = parseClassification(classificationJson);
    result.currentView = currentView;

    if (result.success) {
        log::infof("DescriptorService",
                   "Descriptor result: status=%s confidence=%.2f currentView=%s "
                   "needsRetag=%s recommendedView=%s rotation=%s/%d reason='%s' title='%s'",
                   statusName(result.status),
                   static_cast<double>(result.confidence),
                   smart_tagging::thumbnailViewName(currentView),
                   result.needsRetag ? "true" : "false",
                   smart_tagging::thumbnailViewName(result.recommendedView),
                   result.orientation.needsRotation ? "true" : "false",
                   result.orientation.rotateDegrees,
                   result.viewReason.c_str(),
                   result.title.c_str());
    }

    return result;
}

} // namespace dw
