#include "cam_tool_mapping.h"

#include <algorithm>

namespace dw::cam {
namespace {

constexpr double kInchToMm = 25.4;

double lengthToMm(double value, VtdbUnits units) {
    return units == VtdbUnits::Imperial ? value * kInchToMm : value;
}

} // namespace

double feedToMmPerMin(double value, int rateUnits) {
    switch (rateUnits) {
    case 0: return value * 60.0;             // mm/sec
    case 1: return value;                    // mm/min
    case 2: return value * 1000.0;           // m/min
    case 3: return value * kInchToMm * 60.0; // in/sec
    case 4: return value * kInchToMm;        // in/min
    default: return value;
    }
}

std::optional<EngineTool> toEngineTool(const VtdbToolGeometry& geometry,
                                       const VtdbCuttingData* cuttingData) {
    EngineTool tool;
    switch (geometry.tool_type) {
    case VtdbToolType::BallNose:
    case VtdbToolType::TaperedBallNose:
        tool.type = "ball_endmill";
        break;
    case VtdbToolType::EndMill:
    case VtdbToolType::Radiused:
        tool.type = "flat_endmill";
        break;
    case VtdbToolType::VBit:
        tool.type = "v_bit";
        tool.vBitAngle = geometry.included_angle;
        break;
    case VtdbToolType::Drill:
        tool.type = "drill";
        break;
    default:
        return std::nullopt; // no engine equivalent
    }

    tool.id = geometry.id;
    tool.name = resolveToolNameFormat(geometry);
    tool.diameter = lengthToMm(geometry.diameter, geometry.units);
    tool.flutes = std::max(1, geometry.num_flutes);
    if (tool.diameter <= 0.0)
        return std::nullopt;

    if (cuttingData != nullptr) {
        tool.rpm = cuttingData->spindle_speed;
        tool.feed = feedToMmPerMin(cuttingData->feed_rate, cuttingData->rate_units);
        tool.plungeFeed =
            feedToMmPerMin(cuttingData->plunge_rate, cuttingData->rate_units);
        // Stepdown/stepover are lengths in the cutting data's length units
        // (0 metric, 1 imperial); the engine wants stepover as a fraction of
        // the tool diameter.
        const VtdbUnits lengthUnits =
            cuttingData->length_units == 1 ? VtdbUnits::Imperial : VtdbUnits::Metric;
        tool.stepdown = lengthToMm(cuttingData->stepdown, lengthUnits);
        const double stepoverMm = lengthToMm(cuttingData->stepover, lengthUnits);
        if (stepoverMm > 0.0)
            tool.stepover =
                std::clamp(stepoverMm / tool.diameter, 0.05, 0.95);
    }
    return tool;
}

} // namespace dw::cam
