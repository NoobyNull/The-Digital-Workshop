#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "core/materials/lmstudio_descriptor_service.h"

TEST(LMStudioDescriptorParser, ParsesSmartTaggingFields) {
    dw::LMStudioDescriptorService service;
    auto result = service.parseClassification(R"({
        "status": "retry_view",
        "confidence": 0.58,
        "geometryType": "unknown",
        "needsRetag": true,
        "recommendedView": "front",
        "viewReason": "rear view hides the face",
        "title": "",
        "description": "",
        "hoverNarrative": "",
        "keywords": [],
        "associations": [],
        "categories": []
    })");

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.status, dw::TagClassificationStatus::RetryView);
    EXPECT_FLOAT_EQ(result.confidence, 0.58f);
    EXPECT_EQ(result.geometryType, dw::GeometryType::Unknown);
    EXPECT_TRUE(result.needsRetag);
    EXPECT_EQ(result.recommendedView, dw::ThumbnailView::Front);
    EXPECT_EQ(result.viewReason, "rear view hides the face");
}

TEST(LMStudioDescriptorParser, BuildsRequestWithConfiguredModelName) {
    dw::LMStudioDescriptorService service;
    auto body = service.buildClassificationRequestForTest(
        std::vector<uint8_t>{1, 2, 3}, "llava:latest", dw::ThumbnailView::Front);
    auto json = nlohmann::json::parse(body);

    EXPECT_EQ(json["model"].get<std::string>(), "llava:latest");
}

TEST(LMStudioDescriptorParser, ParsesUnclassifiableWithoutTitle) {
    dw::LMStudioDescriptorService service;
    auto result = service.parseClassification(R"({
        "status": "unclassifiable",
        "confidence": 0.42,
        "geometryType": "flat_part",
        "needsRetag": false,
        "recommendedView": "unknown",
        "viewReason": "generic flat geometry",
        "title": "",
        "description": "",
        "hoverNarrative": "",
        "keywords": [],
        "associations": [],
        "categories": []
    })");

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.status, dw::TagClassificationStatus::Unclassifiable);
    EXPECT_EQ(result.geometryType, dw::GeometryType::FlatPart);
    EXPECT_TRUE(result.title.empty());
    EXPECT_TRUE(result.description.empty());
}

TEST(LMStudioDescriptorParser, ClampsConfidenceIntoValidRange) {
    dw::LMStudioDescriptorService service;
    auto result = service.parseClassification(R"({
        "status": "retry_view",
        "confidence": 1.8,
        "geometryType": "unknown",
        "needsRetag": true,
        "recommendedView": "front",
        "viewReason": "needs another view",
        "title": "",
        "description": "",
        "hoverNarrative": "",
        "keywords": [],
        "associations": [],
        "categories": []
    })");

    ASSERT_TRUE(result.success);
    EXPECT_FLOAT_EQ(result.confidence, 1.0f);
}

TEST(LMStudioDescriptorParser, LimitsAutoCategoriesToFourCleanLevels) {
    dw::LMStudioDescriptorService service;
    auto result = service.parseClassification(R"({
        "status": "final_tag",
        "confidence": 0.92,
        "geometryType": "true_3d",
        "needsRetag": false,
        "recommendedView": "unknown",
        "viewReason": "",
        "title": "Dragon Relief",
        "description": "A carved dragon relief panel for CNC routing.",
        "hoverNarrative": "Dragon relief panel",
        "keywords": ["dragon", "relief"],
        "associations": [],
        "categories": ["Art & Decor", "Relief", "Fantasy", "Dragon", "Winged"]
    })");

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.categories.size(), 4u);
    EXPECT_EQ(result.categories[0], "Relief");
    EXPECT_EQ(result.categories[1], "Fantasy");
    EXPECT_EQ(result.categories[2], "Dragon");
    EXPECT_EQ(result.categories[3], "Winged");
}

TEST(LMStudioDescriptorParser, ParsesOrientationSuggestion) {
    dw::LMStudioDescriptorService service;
    auto result = service.parseClassification(R"({
        "status": "final_tag",
        "confidence": 0.91,
        "geometryType": "2.5d_relief",
        "needsRetag": false,
        "recommendedView": "unknown",
        "viewReason": "",
        "title": "Marine Eagle Emblem",
        "description": "A relief emblem with an eagle that appears sideways.",
        "hoverNarrative": "Sideways Marine eagle emblem",
        "keywords": ["eagle", "emblem"],
        "associations": ["Marines"],
        "categories": ["Military", "Marines", "Emblems"],
        "orientation": {
            "needsRotation": true,
            "uprightView": "front",
            "rotateDegrees": 90,
            "reason": "The eagle head should face upward in the thumbnail."
        }
    })");

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.orientation.needsRotation);
    EXPECT_EQ(result.orientation.uprightView, dw::ThumbnailView::Front);
    EXPECT_EQ(result.orientation.rotateDegrees, 90);
    EXPECT_EQ(result.orientation.reason, "The eagle head should face upward in the thumbnail.");
}

TEST(LMStudioDescriptorParser, ExtractsJsonSchemaResultFromQwenReasoningContent) {
    dw::LMStudioDescriptorService service;
    auto content = service.extractClassificationJson(R"({
        "choices": [{
            "message": {
                "role": "assistant",
                "content": "",
                "reasoning_content": "{\"title\":\"Blue Frame\",\"description\":\"A decorative frame.\"}"
            }
        }]
    })");

    EXPECT_EQ(content, R"({"title":"Blue Frame","description":"A decorative frame."})");
}

TEST(LMStudioDescriptorParser, ClassificationSystemPromptDefinesStructuredOutputContract) {
    dw::LMStudioDescriptorService service;
    const auto prompt = service.classificationSystemPrompt();

    EXPECT_NE(prompt.find("3D/CNC model"), std::string::npos);
    EXPECT_NE(prompt.find("Return only JSON"), std::string::npos);
    EXPECT_NE(prompt.find("retry_view"), std::string::npos);
    EXPECT_NE(prompt.find("one to four"), std::string::npos);
    EXPECT_NE(prompt.find("Never use category names"), std::string::npos);
    EXPECT_NE(prompt.find("3D Print"), std::string::npos);
    EXPECT_NE(prompt.find("stable primary roots"), std::string::npos);
    EXPECT_NE(prompt.find("upright orientation"), std::string::npos);
    EXPECT_NE(prompt.find("Human figures should have heads above torsos"), std::string::npos);
    EXPECT_NE(prompt.find("ground or base should appear below"), std::string::npos);
}

TEST(LMStudioDescriptorParser, ClassificationJsonSchemaRequiresTaggingFields) {
    dw::LMStudioDescriptorService service;
    auto schema = nlohmann::json::parse(service.classificationJsonSchema());

    EXPECT_EQ(schema["type"], "object");
    EXPECT_TRUE(schema["properties"].contains("status"));
    EXPECT_TRUE(schema["properties"].contains("categories"));
    EXPECT_TRUE(schema["properties"].contains("orientation"));
    EXPECT_EQ(schema["properties"]["categories"]["maxItems"], 4);
    EXPECT_EQ(schema["properties"]["orientation"]["properties"]["rotateDegrees"]["enum"][1], 90);
    EXPECT_NE(std::find(schema["required"].begin(), schema["required"].end(), "title"),
              schema["required"].end());
    EXPECT_NE(std::find(schema["required"].begin(), schema["required"].end(), "description"),
              schema["required"].end());
    EXPECT_NE(std::find(schema["required"].begin(), schema["required"].end(), "orientation"),
              schema["required"].end());
}
