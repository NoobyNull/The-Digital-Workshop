#pragma once

namespace dw {

[[nodiscard]] constexpr bool directCarveStepIndicatorNeedsCompactLayout(
    float availableWidth,
    float widestMeasuredLabel,
    int stepCount,
    float minimumGap) noexcept {
    if (stepCount <= 0)
        return false;
    const float requiredWidth = widestMeasuredLabel * static_cast<float>(stepCount) +
                                minimumGap * static_cast<float>(stepCount - 1);
    return requiredWidth > availableWidth;
}

} // namespace dw
