#pragma once

#include <optional>
#include <string>

#include "../types.h"

namespace dw {
namespace carve {

struct DirectCarveProbeResult {
    Vec3 position{0.0f};
    bool contact = false;
};

struct SienciAutoZeroProfile {
    f32 zPlateThicknessMm = 5.0f;
    f32 originOffsetMm = 22.5f;
    f32 finalZRetractMm = 1.0f;
    f32 autoModeApproachMm = 13.0f;
    f32 autoModeSpanMm = 26.0f;
    f32 tipModeApproachMm = 3.0f;
    f32 tipModeSpanMm = 14.0f;
    f32 lateralSearchMm = 30.0f;
};

SienciAutoZeroProfile defaultSienciAutoZeroProfile();

std::optional<DirectCarveProbeResult>
parseGrblProbeResult(const std::string& line);

} // namespace carve
} // namespace dw
