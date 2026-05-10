#include "smart_tagger.h"

#include <algorithm>

#include "../viewport/view_cube_orientation.h"

namespace dw::smart_tagging {

const char* thumbnailViewName(ThumbnailView view) {
    switch (view) {
    case ThumbnailView::Front: return "front";
    case ThumbnailView::Back: return "back";
    case ThumbnailView::Left: return "left";
    case ThumbnailView::Right: return "right";
    case ThumbnailView::Top: return "top";
    case ThumbnailView::Bottom: return "bottom";
    case ThumbnailView::Isometric: return "isometric";
    case ThumbnailView::Unknown: return "unknown";
    }
    return "unknown";
}

const char* tagDecisionActionName(TagDecisionAction action) {
    switch (action) {
    case TagDecisionAction::Accept: return "accept";
    case TagDecisionAction::RetryView: return "retry_view";
    case TagDecisionAction::FallbackIsometric: return "fallback_isometric";
    case TagDecisionAction::Unclassifiable: return "unclassifiable";
    case TagDecisionAction::Failed: return "failed";
    }
    return "failed";
}

ThumbnailView thumbnailViewFromString(const std::string& value) {
    if (value == "front") return ThumbnailView::Front;
    if (value == "back") return ThumbnailView::Back;
    if (value == "left") return ThumbnailView::Left;
    if (value == "right") return ThumbnailView::Right;
    if (value == "top") return ThumbnailView::Top;
    if (value == "bottom") return ThumbnailView::Bottom;
    if (value == "isometric") return ThumbnailView::Isometric;
    return ThumbnailView::Unknown;
}

bool isPerpendicularView(ThumbnailView view) {
    return view == ThumbnailView::Front || view == ThumbnailView::Back ||
           view == ThumbnailView::Left || view == ThumbnailView::Right ||
           view == ThumbnailView::Top || view == ThumbnailView::Bottom;
}

bool hasTriedView(const std::vector<ThumbnailView>& triedViews, ThumbnailView view) {
    return std::find(triedViews.begin(), triedViews.end(), view) != triedViews.end();
}

ViewCamera cameraForView(ThumbnailView view, f32 currentYawDeg) {
    switch (view) {
    case ThumbnailView::Front:
        return {view, 0.0f, 0.0f};
    case ThumbnailView::Back:
        return {view, 180.0f, 0.0f};
    case ThumbnailView::Left:
        return {view, 90.0f, 0.0f};
    case ThumbnailView::Right:
        return {view, 270.0f, 0.0f};
    case ThumbnailView::Top: {
        auto snap = snapViewCubeOrientation(ViewCubeFace::Top, currentYawDeg);
        return {view, snap.yawDeg, snap.pitchDeg};
    }
    case ThumbnailView::Bottom: {
        auto snap = snapViewCubeOrientation(ViewCubeFace::Bottom, currentYawDeg);
        return {view, snap.yawDeg, snap.pitchDeg};
    }
    case ThumbnailView::Isometric:
        return {view, 45.0f, 35.0f};
    case ThumbnailView::Unknown:
        return {view, 0.0f, 0.0f};
    }
    return {ThumbnailView::Unknown, 0.0f, 0.0f};
}

Mat4 orientationCorrectionMatrix(int clockwiseDegrees) {
    int normalized = clockwiseDegrees % 360;
    if (normalized < 0)
        normalized += 360;
    if (normalized != 90 && normalized != 180 && normalized != 270)
        normalized = 0;

    float counterClockwise = static_cast<float>((360 - normalized) % 360);
    return glm::rotate(Mat4(1.0f), glm::radians(counterClockwise), Vec3{0.0f, 0.0f, 1.0f});
}

TagDecision decideNextStep(const DescriptorResult& result,
                           const std::vector<ThumbnailView>& triedViews,
                           int perpendicularRetries) {
    if (!result.success) {
        return {TagDecisionAction::Failed, ThumbnailView::Unknown};
    }

    if (result.status == TagClassificationStatus::Unclassifiable) {
        return {TagDecisionAction::Unclassifiable, ThumbnailView::Unknown};
    }

    if (result.status == TagClassificationStatus::FallbackIsometric) {
        return hasTriedView(triedViews, ThumbnailView::Isometric)
                   ? TagDecision{TagDecisionAction::Unclassifiable, ThumbnailView::Unknown}
                   : TagDecision{TagDecisionAction::FallbackIsometric, ThumbnailView::Isometric};
    }

    if ((result.status == TagClassificationStatus::RetryView || result.needsRetag) &&
        isPerpendicularView(result.recommendedView) &&
        !hasTriedView(triedViews, result.recommendedView) &&
        perpendicularRetries < kMaxPerpendicularRetries) {
        return {TagDecisionAction::RetryView, result.recommendedView};
    }

    if (result.status == TagClassificationStatus::RetryView || result.needsRetag) {
        return hasTriedView(triedViews, ThumbnailView::Isometric)
                   ? TagDecision{TagDecisionAction::Unclassifiable, ThumbnailView::Unknown}
                   : TagDecision{TagDecisionAction::FallbackIsometric, ThumbnailView::Isometric};
    }

    if (result.confidence >= kHighConfidenceThreshold &&
        !result.title.empty() && !result.description.empty()) {
        return {TagDecisionAction::Accept, ThumbnailView::Unknown};
    }

    if (hasTriedView(triedViews, ThumbnailView::Isometric)) {
        if (result.status == TagClassificationStatus::FinalTag &&
            result.confidence >= kFallbackAcceptanceConfidence &&
            !result.title.empty() && !result.description.empty()) {
            return {TagDecisionAction::Accept, ThumbnailView::Unknown};
        }
        return {TagDecisionAction::Unclassifiable, ThumbnailView::Unknown};
    }

    if (result.confidence < kMinimumUsefulConfidence) {
        return {TagDecisionAction::Unclassifiable, ThumbnailView::Unknown};
    }

    return {TagDecisionAction::FallbackIsometric, ThumbnailView::Isometric};
}

} // namespace dw::smart_tagging
