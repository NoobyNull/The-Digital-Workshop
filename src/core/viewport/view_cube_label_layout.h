#pragma once

#include <array>
#include <cmath>

#include "../types.h"

namespace dw {

struct ViewCubeScreenPoint {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

namespace view_cube_label_detail {

inline f32 cross(ViewCubeScreenPoint start,
                 ViewCubeScreenPoint end,
                 ViewCubeScreenPoint point) {
    return (end.x - start.x) * (point.y - start.y) -
           (end.y - start.y) * (point.x - start.x);
}

inline bool pointInsideConvexQuad(
    ViewCubeScreenPoint point,
    const std::array<ViewCubeScreenPoint, 4>& quad) {
    constexpr f32 kEpsilon = 0.001f;
    const f32 c0 = cross(quad[0], quad[1], point);
    const f32 c1 = cross(quad[1], quad[2], point);
    const f32 c2 = cross(quad[2], quad[3], point);
    const f32 c3 = cross(quad[3], quad[0], point);
    const bool nonNegative = c0 >= -kEpsilon && c1 >= -kEpsilon &&
                             c2 >= -kEpsilon && c3 >= -kEpsilon;
    const bool nonPositive = c0 <= kEpsilon && c1 <= kEpsilon &&
                             c2 <= kEpsilon && c3 <= kEpsilon;
    return nonNegative || nonPositive;
}

inline f32 doubledArea(const std::array<ViewCubeScreenPoint, 4>& quad) {
    f32 area = 0.0f;
    for (usize index = 0; index < quad.size(); ++index) {
        const auto& current = quad[index];
        const auto& next = quad[(index + 1) % quad.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return std::abs(area);
}

} // namespace view_cube_label_detail

// Labels are not clipped to their projected face by ImGui. Require the whole
// padded label rectangle to fit inside the visible face so an edge-on face
// cannot draw text through a nearer face. This affects presentation only; face
// geometry and hit targets remain unchanged.
inline bool viewCubeFaceCanContainLabel(
    const std::array<ViewCubeScreenPoint, 4>& face,
    f32 labelWidth,
    f32 labelHeight,
    f32 padding) {
    constexpr f32 kMinimumArea = 0.01f;
    if (labelWidth <= 0.0f || labelHeight <= 0.0f || padding < 0.0f ||
        view_cube_label_detail::doubledArea(face) <= kMinimumArea) {
        return false;
    }

    ViewCubeScreenPoint center;
    for (const auto& point : face) {
        center.x += point.x * 0.25f;
        center.y += point.y * 0.25f;
    }

    const f32 halfWidth = labelWidth * 0.5f + padding;
    const f32 halfHeight = labelHeight * 0.5f + padding;
    const std::array<ViewCubeScreenPoint, 4> labelCorners = {{
        {center.x - halfWidth, center.y - halfHeight},
        {center.x + halfWidth, center.y - halfHeight},
        {center.x + halfWidth, center.y + halfHeight},
        {center.x - halfWidth, center.y + halfHeight},
    }};

    for (const auto& corner : labelCorners) {
        if (!view_cube_label_detail::pointInsideConvexQuad(corner, face)) {
            return false;
        }
    }
    return true;
}

} // namespace dw
