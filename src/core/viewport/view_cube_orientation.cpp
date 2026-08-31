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

f32 rotateViewYawByQuarterTurns(f32 currentYawDeg, int quarterTurns) {
    return normalizeYaw(currentYawDeg + static_cast<f32>(quarterTurns) * 90.0f);
}

ViewCubeOrientation rotateViewPitchByQuarterTurns(f32 currentYawDeg,
                                                  f32 currentPitchDeg,
                                                  int quarterTurns) {
    f32 yaw = normalizeYaw(currentYawDeg);
    f32 pitch = currentPitchDeg;

    int steps = std::abs(quarterTurns);
    int direction = (quarterTurns >= 0) ? 1 : -1;
    for (int i = 0; i < steps; ++i) {
        if (direction > 0) {
            if (pitch > 45.0f) {
                yaw = normalizeYaw(yaw + 180.0f);
                pitch = 0.0f;
            } else if (pitch < -45.0f) {
                pitch = 0.0f;
            } else {
                pitch = 89.0f;
            }
        } else {
            if (pitch < -45.0f) {
                yaw = normalizeYaw(yaw + 180.0f);
                pitch = 0.0f;
            } else if (pitch > 45.0f) {
                pitch = 0.0f;
            } else {
                pitch = -89.0f;
            }
        }
    }

    return {yaw, pitch};
}

} // namespace dw
