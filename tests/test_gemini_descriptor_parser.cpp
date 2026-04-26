#include <gtest/gtest.h>

#include "core/materials/gemini_descriptor_service.h"

TEST(GeminiDescriptorParser, ParsesSmartTaggingFields) {
    dw::GeminiDescriptorService service;
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

TEST(GeminiDescriptorParser, ParsesUnclassifiableWithoutTitle) {
    dw::GeminiDescriptorService service;
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

TEST(GeminiDescriptorParser, ClampsConfidenceIntoValidRange) {
    dw::GeminiDescriptorService service;
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
