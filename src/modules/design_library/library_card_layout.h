#pragma once

namespace dw::design_library {

struct LibraryCardLabelLayout {
    float wrapWidth = 0.0F;
    float labelHeight = 0.0F;
    float cellHeight = 0.0F;
};

// Converts the UI toolkit's measured wrapped-text height into the full card
// allocation. Keeping this policy render-independent prevents a card from
// drawing multiple lines into a one-line clip rectangle.
[[nodiscard]] LibraryCardLabelLayout makeLibraryCardLabelLayout(
    float thumbnailExtent,
    float measuredWrappedTextHeight,
    float singleLineTextHeight,
    float outerPadding,
    float labelGap,
    float labelBottomPadding);

} // namespace dw::design_library
