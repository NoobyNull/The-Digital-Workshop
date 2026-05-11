#pragma once

#include <algorithm>
#include <cstddef>

namespace dw::gcode {

inline constexpr std::size_t kDefaultMaxViewportSegments = 250000;

inline std::size_t viewportSegmentStride(
    std::size_t segmentCount,
    std::size_t maxViewportSegments = kDefaultMaxViewportSegments)
{
    if (maxViewportSegments == 0 || segmentCount <= maxViewportSegments) {
        return 1;
    }
    return std::max<std::size_t>(
        1, (segmentCount + maxViewportSegments - 1) / maxViewportSegments);
}

inline bool shouldIncludeViewportSegment(
    std::size_t index,
    std::size_t segmentCount,
    std::size_t stride)
{
    if (stride <= 1 || index == 0 || index + 1 == segmentCount) {
        return true;
    }
    return index % stride == 0;
}

} // namespace dw::gcode
