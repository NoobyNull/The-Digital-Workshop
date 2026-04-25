#include "view_cube_orientation.h"

#include <cmath>

namespace dw {

namespace {

f32 normalizeYaw(f32 yawDeg) {
    f32 normalized = std::fmod(yawDeg, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    return normalized;
}

f32 nearestRightAngleYaw(f32 yawDeg) {
    f32 snapped = std::round(normalizeYaw(yawDeg) / 90.0f) * 90.0f;
    if (snapped >= 360.0f) {
        snapped -= 360.0f;
    }
    return snapped;
}

} // namespace

ViewCubeOrientation snapViewCubeOrientation(ViewCubeFace face, f32 currentYawDeg) {
    switch (face) {
    case ViewCubeFace::Front:
        return {0.0f, 0.0f};
    case ViewCubeFace::Back:
        return {180.0f, 0.0f};
    case ViewCubeFace::Left:
        return {90.0f, 0.0f};
    case ViewCubeFace::Right:
        return {270.0f, 0.0f};
    case ViewCubeFace::Top:
        return {nearestRightAngleYaw(currentYawDeg), 89.0f};
    case ViewCubeFace::Bottom:
        return {nearestRightAngleYaw(currentYawDeg), -89.0f};
    }
    return {0.0f, 0.0f};
}

} // namespace dw
