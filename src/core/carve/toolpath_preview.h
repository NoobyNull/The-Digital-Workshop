#pragma once

#include "toolpath_types.h"

#include <cstddef>
#include <vector>

namespace dw::carve {

// A top-down preview stroke represents one uninterrupted cutting pass and the
// rapid move that positions the cutter for it.  The source toolpath may contain
// hundreds of collinear interpolation points per pass; those points are not
// useful to a 2D display, so this geometry intentionally stores only XY bends.
struct ToolpathPreviewStroke {
    std::vector<Vec3> rapidPoints;
    std::vector<Vec3> cutPoints;
};

struct ToolpathPreviewGeometry {
    std::vector<ToolpathPreviewStroke> strokes;
    std::size_t sourceMoveCount = 0;
};

struct MultiPassToolpathPreviewGeometry {
    ToolpathPreviewGeometry clearing;
    ToolpathPreviewGeometry finishing;
};

struct ToolpathPreviewSelection {
    std::vector<std::size_t> strokeIndices;
    std::size_t totalStrokeCount = 0;

    [[nodiscard]] bool simplified() const {
        return strokeIndices.size() < totalStrokeCount;
    }
};

// Build and cache this when the toolpath changes.  The result preserves every
// distinct cutting stroke while collapsing top-down collinear interpolation.
ToolpathPreviewGeometry buildToolpathPreviewGeometry(const Toolpath& toolpath);
MultiPassToolpathPreviewGeometry buildToolpathPreviewGeometry(
    const MultiPassToolpath& toolpath);

// Select representative strokes for the current screen scale. Parallel passes
// closer than minimumSpacingPixels share a display cell, while differently
// oriented passes remain independently visible.  Zooming therefore reveals
// progressively more of the real path without rebuilding raw G-code geometry.
ToolpathPreviewSelection selectToolpathPreviewStrokes(
    const ToolpathPreviewGeometry& geometry,
    f32 pixelsPerMmX,
    f32 pixelsPerMmY,
    f32 minimumSpacingPixels = 3.0f,
    std::size_t maximumStrokes = 2000);

} // namespace dw::carve
