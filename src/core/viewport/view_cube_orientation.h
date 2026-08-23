#pragma once

#include "../types.h"

namespace dw {

enum class ViewCubeFace {
    Back,
    Front,
    Right,
    Left,
    Top,
    Bottom,
};

struct ViewCubeOrientation {
    f32 yawDeg = 0.0f;
    f32 pitchDeg = 0.0f;
};

ViewCubeOrientation snapViewCubeOrientation(ViewCubeFace face, f32 currentYawDeg);
f32 rotateViewYawByQuarterTurns(f32 currentYawDeg, int quarterTurns);
ViewCubeOrientation rotateViewPitchByQuarterTurns(f32 currentYawDeg,
                                                  f32 currentPitchDeg,
                                                  int quarterTurns);

} // namespace dw
