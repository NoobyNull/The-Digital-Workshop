#pragma once

#include <vector>

#include "../materials/gemini_descriptor_service.h"
#include "../types.h"

namespace dw::smart_tagging {

constexpr int kMaxPerpendicularRetries = 2;
constexpr float kHighConfidenceThreshold = 0.95f;
constexpr float kMinimumUsefulConfidence = 0.75f;

enum class TagDecisionAction {
    Accept,
    RetryView,
    FallbackIsometric,
    Unclassifiable,
    Failed,
};

struct ViewCamera {
    ThumbnailView view = ThumbnailView::Unknown;
    f32 yawDeg = 0.0f;
    f32 pitchDeg = 0.0f;
};

struct TagDecision {
    TagDecisionAction action = TagDecisionAction::Failed;
    ThumbnailView nextView = ThumbnailView::Unknown;
};

[[nodiscard]] const char* thumbnailViewName(ThumbnailView view);
[[nodiscard]] const char* tagDecisionActionName(TagDecisionAction action);
[[nodiscard]] ThumbnailView thumbnailViewFromString(const std::string& value);
[[nodiscard]] bool isPerpendicularView(ThumbnailView view);
[[nodiscard]] bool hasTriedView(const std::vector<ThumbnailView>& triedViews, ThumbnailView view);
[[nodiscard]] ViewCamera cameraForView(ThumbnailView view, f32 currentYawDeg);
[[nodiscard]] TagDecision decideNextStep(const DescriptorResult& result,
                                         const std::vector<ThumbnailView>& triedViews,
                                         int perpendicularRetries);

} // namespace dw::smart_tagging
