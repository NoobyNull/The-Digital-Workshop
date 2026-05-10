#include <gtest/gtest.h>

#include "core/import/smart_tagger.h"

TEST(SmartTagger, AcceptsSpecificHighConfidenceTag) {
    dw::DescriptorResult result;
    result.success = true;
    result.status = dw::TagClassificationStatus::FinalTag;
    result.confidence = 0.96f;
    result.title = "Hellboy Bust";
    result.description = "A recognizable horned character bust.";

    auto decision = dw::smart_tagging::decideNextStep(result, {}, 0);

    EXPECT_EQ(decision.action, dw::smart_tagging::TagDecisionAction::Accept);
}

TEST(SmartTagger, RetriesRequestedPerpendicularViewWhenUntried) {
    dw::DescriptorResult result;
    result.success = true;
    result.status = dw::TagClassificationStatus::RetryView;
    result.confidence = 0.58f;
    result.needsRetag = true;
    result.recommendedView = dw::ThumbnailView::Front;

    auto decision = dw::smart_tagging::decideNextStep(
        result, {dw::ThumbnailView::Back}, 1);

    EXPECT_EQ(decision.action, dw::smart_tagging::TagDecisionAction::RetryView);
    EXPECT_EQ(decision.nextView, dw::ThumbnailView::Front);
}

TEST(SmartTagger, UsesIsometricFallbackAfterPerpendicularRetryLimit) {
    dw::DescriptorResult result;
    result.success = true;
    result.status = dw::TagClassificationStatus::RetryView;
    result.needsRetag = true;
    result.recommendedView = dw::ThumbnailView::Left;

    auto decision = dw::smart_tagging::decideNextStep(
        result, {dw::ThumbnailView::Back, dw::ThumbnailView::Front}, 2);

    EXPECT_EQ(decision.action, dw::smart_tagging::TagDecisionAction::FallbackIsometric);
    EXPECT_EQ(decision.nextView, dw::ThumbnailView::Isometric);
}

TEST(SmartTagger, MarksGenericFallbackAsUnclassifiable) {
    dw::DescriptorResult result;
    result.success = true;
    result.status = dw::TagClassificationStatus::FinalTag;
    result.confidence = 0.76f;
    result.title = "Wavy CNC Panel Blanks";
    result.description = "Generic flat panel parts.";

    auto decision = dw::smart_tagging::decideNextStep(
        result, {dw::ThumbnailView::Top, dw::ThumbnailView::Isometric}, 2);

    EXPECT_EQ(decision.action, dw::smart_tagging::TagDecisionAction::Unclassifiable);
}

TEST(SmartTagger, AcceptsUsableFinalTagAfterIsometricFallback) {
    dw::DescriptorResult result;
    result.success = true;
    result.status = dw::TagClassificationStatus::FinalTag;
    result.confidence = 0.8f;
    result.title = "Decorative Floral Relief Panel";
    result.description = "A carved floral relief panel with a central medallion.";

    auto decision = dw::smart_tagging::decideNextStep(
        result, {dw::ThumbnailView::Unknown, dw::ThumbnailView::Isometric}, 0);

    EXPECT_EQ(decision.action, dw::smart_tagging::TagDecisionAction::Accept);
}

TEST(SmartTagger, NamesDecisionActionsForLogs) {
    EXPECT_STREQ(dw::smart_tagging::tagDecisionActionName(
                     dw::smart_tagging::TagDecisionAction::RetryView),
                 "retry_view");
    EXPECT_STREQ(dw::smart_tagging::tagDecisionActionName(
                     dw::smart_tagging::TagDecisionAction::Unclassifiable),
                 "unclassifiable");
}

TEST(SmartTagger, ConvertsNamedViewsToCameraAngles) {
    auto front = dw::smart_tagging::cameraForView(dw::ThumbnailView::Front, 180.0f);
    auto top = dw::smart_tagging::cameraForView(dw::ThumbnailView::Top, 181.0f);
    auto iso = dw::smart_tagging::cameraForView(dw::ThumbnailView::Isometric, 0.0f);

    EXPECT_FLOAT_EQ(front.yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(front.pitchDeg, 0.0f);
    EXPECT_FLOAT_EQ(top.yawDeg, 180.0f);
    EXPECT_FLOAT_EQ(top.pitchDeg, 89.0f);
    EXPECT_FLOAT_EQ(iso.yawDeg, 45.0f);
    EXPECT_FLOAT_EQ(iso.pitchDeg, 35.0f);
}

TEST(SmartTagger, ConvertsClockwiseImageRotationToModelOrientationCorrection) {
    auto correction = dw::smart_tagging::orientationCorrectionMatrix(270);
    auto x = correction * dw::Vec4{1.0f, 0.0f, 0.0f, 1.0f};

    EXPECT_NEAR(x.x, 0.0f, 0.0001f);
    EXPECT_NEAR(x.y, 1.0f, 0.0001f);
    EXPECT_NEAR(x.z, 0.0f, 0.0001f);
}
