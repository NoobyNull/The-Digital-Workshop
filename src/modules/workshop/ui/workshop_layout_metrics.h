#pragma once

#include <algorithm>

namespace dw::workshop::ui {

struct SideColumnLayout {
    float sideWidth = 0.0F;
    float centerWidth = 0.0F;
    float splitRatio = 0.0F;
};

struct SideColumnSpec {
    float widthInEms = 0.0F;
    float minimumWidth = 0.0F;
    float maximumWidth = 0.0F;
};

// Prepare and Preview keep a slicer-style settings column on the left.
inline constexpr SideColumnSpec kSettingsSidebar{26.0F, 320.0F, 520.0F};
// Device keeps the machine controls (position, jog, safety) on the right.
inline constexpr SideColumnSpec kMachineControlColumn{28.0F, 340.0F, 560.0F};
// Library gives the design browser most of the width and previews beside it.
inline constexpr SideColumnSpec kLibraryBrowser{48.0F, 520.0F, 1100.0F};

struct HomeLayout {
    float contentWidth = 0.0F;
    float horizontalOffset = 0.0F;
    float leftColumnWidth = 0.0F;
    float rightColumnWidth = 0.0F;
};

// Side columns carry long labels. Size them from the rendered font instead of
// a viewport percentage, then preserve enough center workspace for the canvas.
inline SideColumnLayout chooseSideColumn(float workAreaWidth,
                                         float fontSize,
                                         SideColumnSpec spec) noexcept {
    constexpr float kMinimumCenterWidth = 480.0F;

    const float width = std::max(0.0F, workAreaWidth);
    const float em = std::max(1.0F, fontSize);
    const float requested =
        std::clamp(em * spec.widthInEms, spec.minimumWidth, spec.maximumWidth);
    const float side = std::min(requested, std::max(0.0F, width - kMinimumCenterWidth));

    SideColumnLayout layout;
    layout.sideWidth = side;
    layout.centerWidth = width - side;
    layout.splitRatio = width > 0.0F ? side / width : 0.0F;
    return layout;
}

// Home should stay compact at ordinary desktop widths and become a centered,
// readable block instead of stretching its cards across a 4K work area.
inline HomeLayout chooseHomeLayout(float availableWidth,
                                               float columnSpacing) noexcept {
    constexpr float kMaximumContentWidth = 1440.0F;
    constexpr float kLeftColumnShare = 0.55F;

    const float width = std::max(0.0F, availableWidth);
    const float spacing = std::clamp(columnSpacing, 0.0F, width);

    HomeLayout layout;
    layout.contentWidth = std::min(width, kMaximumContentWidth);
    layout.horizontalOffset = (width - layout.contentWidth) * 0.5F;
    layout.leftColumnWidth = layout.contentWidth * kLeftColumnShare;
    layout.rightColumnWidth =
        std::max(0.0F, layout.contentWidth - layout.leftColumnWidth - spacing);
    return layout;
}

} // namespace dw::workshop::ui
