#pragma once

#include "types.h"

namespace dw {

/// Convert a G-code space coordinate (Z-up) to renderer space (Y-up).
/// Swaps the Y and Z components: renderer.x = gcode.x,
///                                renderer.y = gcode.z,
///                                renderer.z = gcode.y
inline Vec3 gcodeToRenderer(Vec3 v) {
    return Vec3{v.x, v.z, v.y};
}

} // namespace dw
