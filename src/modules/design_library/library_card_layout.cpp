#include "library_card_layout.h"

#include <algorithm>

namespace dw::design_library {

LibraryCardLabelLayout makeLibraryCardLabelLayout(float thumbnailExtent,
                                                  float measuredWrappedTextHeight,
                                                  float singleLineTextHeight,
                                                  float outerPadding,
                                                  float labelGap,
                                                  float labelBottomPadding) {
    const float width = std::max(0.0F, thumbnailExtent);
    const float padding = std::max(0.0F, outerPadding);
    const float gap = std::max(0.0F, labelGap);
    const float bottomPadding = std::max(0.0F, labelBottomPadding);
    const float labelHeight = std::max({0.0F,
                                        measuredWrappedTextHeight,
                                        singleLineTextHeight}) +
                              bottomPadding;

    return {width,
            labelHeight,
            padding + width + gap + labelHeight + padding};
}

} // namespace dw::design_library
