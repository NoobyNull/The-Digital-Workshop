#pragma once

#include <optional>
#include <string>

#include "../cnc/cnc_tool.h"

namespace dw::cam {

// Engine-side tool definition (bridge spec.ts ToolSpec), metric throughout:
// mm lengths, mm/min feeds, stepover as a fraction of diameter.
struct EngineTool {
    std::string id;
    std::string name;
    std::string type; // flat_endmill | ball_endmill | v_bit | drill
    double diameter = 0.0;
    double vBitAngle = 0.0; // degrees, v_bit only
    int flutes = 2;
    int rpm = 0;
    double feed = 0.0;
    double plungeFeed = 0.0;
    double stepdown = 0.0;
    double stepover = 0.0;
};

// Vectric rate_units -> mm/min. Values per the .vtdb format:
// 0 mm/sec, 1 mm/min, 2 m/min, 3 in/sec, 4 in/min.
[[nodiscard]] double feedToMmPerMin(double value, int rateUnits);

// Project a .vtdb tool (geometry + optional cutting data) into an engine
// tool. Returns nullopt for tool types the engine has no equivalent for
// (thread mills, form tools, laser). This is the Phase 4 interpreter's
// forward direction, minimally.
[[nodiscard]] std::optional<EngineTool> toEngineTool(const VtdbToolGeometry& geometry,
                                                     const VtdbCuttingData* cuttingData);

} // namespace dw::cam
