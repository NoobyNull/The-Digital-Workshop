#pragma once

#include <algorithm>

namespace dw::workshop::ui {

struct GuidedDockLayout {
    float leftWidth = 0.0F;
    float rightWidth = 0.0F;
    float centerWidth = 0.0F;
    float leftSplitRatio = 0.0F;
    float rightSplitRatio = 0.0F;
};

struct GuidedHomeLayout {
    float contentWidth = 0.0F;
    float horizontalOffset = 0.0F;
    float leftColumnWidth = 0.0F;
    float rightColumnWidth = 0.0F;
};

// Guided sidebars have long, novice-facing labels. Size them from the rendered
// font instead of a viewport percentage, then preserve enough center workspace.
inline GuidedDockLayout chooseGuidedDockLayout(float workAreaWidth,
                                                float fontSize) noexcept {
    constexpr float kMinimumLeftWidth = 260.0F;
    constexpr float kMaximumLeftWidth = 430.0F;
    constexpr float kLeftWidthInEms = 17.5F;
    constexpr float kMinimumRightWidth = 220.0F;
    constexpr float kMaximumRightWidth = 320.0F;
    constexpr float kRightWidthInEms = 12.0F;
    constexpr float kMinimumCenterWidth = 480.0F;

    const float width = std::max(0.0F, workAreaWidth);
    const float em = std::max(1.0F, fontSize);
    float left = std::clamp(em * kLeftWidthInEms,
                            kMinimumLeftWidth,
                            kMaximumLeftWidth);
    float right = std::clamp(em * kRightWidthInEms,
                             kMinimumRightWidth,
                             kMaximumRightWidth);

    const float sideBudget = std::max(0.0F, width - kMinimumCenterWidth);
    const float requestedSides = left + right;
    if (requestedSides > sideBudget && requestedSides > 0.0F) {
        const float scale = sideBudget / requestedSides;
        left *= scale;
        right *= scale;
    }

    const float remainingAfterLeft = std::max(0.0F, width - left);
    GuidedDockLayout layout;
    layout.leftWidth = left;
    layout.rightWidth = std::min(right, remainingAfterLeft);
    layout.centerWidth = std::max(0.0F, width - left - layout.rightWidth);
    layout.leftSplitRatio = width > 0.0F ? left / width : 0.0F;
    layout.rightSplitRatio = remainingAfterLeft > 0.0F
        ? layout.rightWidth / remainingAfterLeft
        : 0.0F;
    return layout;
}

// Home should stay compact at ordinary desktop widths and become a centered,
// readable block instead of stretching its cards across a 4K work area.
inline GuidedHomeLayout chooseGuidedHomeLayout(float availableWidth,
                                               float columnSpacing) noexcept {
    constexpr float kMaximumContentWidth = 1440.0F;
    constexpr float kLeftColumnShare = 0.55F;

    const float width = std::max(0.0F, availableWidth);
    const float spacing = std::clamp(columnSpacing, 0.0F, width);

    GuidedHomeLayout layout;
    layout.contentWidth = std::min(width, kMaximumContentWidth);
    layout.horizontalOffset = (width - layout.contentWidth) * 0.5F;
    layout.leftColumnWidth = layout.contentWidth * kLeftColumnShare;
    layout.rightColumnWidth =
        std::max(0.0F, layout.contentWidth - layout.leftColumnWidth - spacing);
    return layout;
}

} // namespace dw::workshop::ui
