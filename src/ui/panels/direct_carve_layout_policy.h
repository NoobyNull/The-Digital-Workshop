#pragma once

#include <algorithm>
#include <cstddef>

namespace dw {

struct DirectCarveTaskLayout {
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;
    float horizontalOffset = 0.0F;
};

// Keep the task, its progress, and its actions in one readable region. The
// limits scale with the active font, so high-DPI controls gain room without a
// 4K dock stretching a sentence or progress bar across the whole display.
[[nodiscard]] inline DirectCarveTaskLayout chooseDirectCarveTaskLayout(float availableWidth,
                                                                       float availableHeight,
                                                                       float fontSize) noexcept {
    constexpr float kMaximumWidthInEms = 72.0F;
    constexpr float kMaximumHeightInEms = 38.0F;

    const float width = std::max(0.0F, availableWidth);
    const float height = std::max(0.0F, availableHeight);
    const float em = std::max(1.0F, fontSize);

    DirectCarveTaskLayout layout;
    layout.contentWidth = std::min(width, em * kMaximumWidthInEms);
    layout.contentHeight = std::min(height, em * kMaximumHeightInEms);
    layout.horizontalOffset = (width - layout.contentWidth) * 0.5F;
    return layout;
}

struct DirectCarveFooterLayout {
    bool stacked = false;
    float backWidth = 0.0F;
    float primaryWidth = 0.0F;
    float cancelWidth = 0.0F;
    float controlsHeight = 0.0F;
};

struct DirectCarveToolListLayout {
    float height = 0.0F;
    bool scrolls = false;
};

// Tool rows shrink to their actual content when the library is short, while a
// large catalog owns its scrolling inside the table. The caller supplies the
// exact space required by the selected-tool summary beneath the list so the
// outer preparation body does not gain a second scrollbar in normal layouts.
[[nodiscard]] inline DirectCarveToolListLayout chooseDirectCarveToolListLayout(
    float availableHeight,
    std::size_t rowCount,
    float rowHeight,
    float headerHeight,
    float belowListReserve,
    float borderSize = 1.0F) noexcept {
    const float available = std::max(0.0F, availableHeight);
    const float row = std::max(1.0F, rowHeight);
    const float header = std::max(0.0F, headerHeight);
    const float border = std::max(0.0F, borderSize) * 2.0F;
    const float usable = std::max(0.0F, available - std::max(0.0F, belowListReserve));
    const float content = header + border +
                          row * static_cast<float>(std::max<std::size_t>(rowCount, 1U));

    DirectCarveToolListLayout layout;
    layout.height = std::min(content, usable);
    layout.scrolls = content > layout.height + 0.5F;
    return layout;
}

// Reserve the exact vertical space consumed after the scrolling step body:
// EndChild spacing, separator spacing, explicit spacing, controls, and the
// optional note (which has its own leading item spacing). Keeping this math in
// one policy prevents the non-scrolling task shell from overflowing at scaled
// UI sizes, where SeparatorSize is larger than one pixel.
[[nodiscard]] inline float directCarveStickyFooterReserveHeight(float controlsHeight,
                                                                float noteTextHeight,
                                                                bool showNote,
                                                                float verticalSpacing,
                                                                float separatorSize) noexcept {
    const float spacing = std::max(0.0F, verticalSpacing);
    const float separator = std::max(1.0F, separatorSize);
    const float note = showNote ? std::max(0.0F, noteTextHeight) + spacing : 0.0F;
    return std::max(0.0F, controlsHeight) + note + spacing * 3.0F + separator;
}

// Button widths are derived from the rendered labels. If the full row cannot
// fit, the primary action gets its own row and Back/Cancel share the next one.
[[nodiscard]] inline DirectCarveFooterLayout chooseDirectCarveFooterLayout(
    float availableWidth,
    float fontSize,
    float frameHeight,
    float horizontalSpacing,
    float verticalSpacing,
    float horizontalFramePadding,
    float backLabelWidth,
    float primaryLabelWidth,
    float cancelLabelWidth) noexcept {
    const float width = std::max(0.0F, availableWidth);
    const float em = std::max(1.0F, fontSize);
    const float minimumButtonWidth = em * 7.5F;
    const float labelPadding = std::max(0.0F, horizontalFramePadding) * 2.0F;

    DirectCarveFooterLayout layout;
    layout.backWidth = std::max(minimumButtonWidth, std::max(0.0F, backLabelWidth) + labelPadding);
    layout.primaryWidth = std::max(minimumButtonWidth,
                                   std::max(0.0F, primaryLabelWidth) + labelPadding);
    layout.cancelWidth = std::max(minimumButtonWidth,
                                  std::max(0.0F, cancelLabelWidth) + labelPadding);

    const float spacing = std::max(0.0F, horizontalSpacing);
    const float requested = layout.backWidth + layout.primaryWidth + layout.cancelWidth +
                            spacing * 2.0F;
    if (requested <= width) {
        layout.controlsHeight = std::max(0.0F, frameHeight);
        return layout;
    }

    layout.stacked = true;
    layout.primaryWidth = width;
    layout.backWidth = std::max(0.0F, (width - spacing) * 0.5F);
    layout.cancelWidth = layout.backWidth;
    layout.controlsHeight = std::max(0.0F, frameHeight) * 2.0F + std::max(0.0F, verticalSpacing);
    return layout;
}

[[nodiscard]] inline bool directCarveManualToolUsesCompactRow(float availableWidth,
                                                              float fontSize) noexcept {
    return std::max(0.0F, availableWidth) >= std::max(1.0F, fontSize) * 28.0F;
}

} // namespace dw
