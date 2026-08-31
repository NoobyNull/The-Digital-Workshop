#include "cam_job_spec.h"

#include <nlohmann/json.hpp>

namespace dw::cam {

std::string buildDefaultSurfacingJobSpec(const CamJobRequest& request) {
    // ponytail: fixed default tooling until Phase 4 maps .vtdb tools into
    // CAMJ definitions; values match cambridge/bridge/examples/dome-fluidnc.json.
    nlohmann::json spec = {
        {"name", request.modelName},
        {"units", "mm"},
        {"machine", request.machineId},
        {"stock", "auto"},
        {"stockMargin", 2},
        {"tools",
         {{{"id", "rough"},
           {"type", "flat_endmill"},
           {"diameter", 6},
           {"flutes", 2},
           {"rpm", 18000},
           {"feed", 1500},
           {"plungeFeed", 500},
           {"stepdown", 2},
           {"stepover", 0.45}},
          {{"id", "finish"},
           {"type", "ball_endmill"},
           {"diameter", 3},
           {"flutes", 2},
           {"rpm", 18000},
           {"feed", 1200},
           {"plungeFeed", 400},
           {"stepdown", 1},
           {"stepover", 0.12}}}},
        {"features",
         {{{"type", "mesh"},
           {"id", "model"},
           {"path", request.meshPath},
           {"axisSwap", request.axisSwap.empty() ? "none" : request.axisSwap}}}},
        {"operations",
         {{{"kind", "rough_surface"},
           {"target", {"model"}},
           {"tool", "rough"},
           {"stockToLeaveAxial", 0.5},
           {"stockToLeaveRadial", 0.5}},
          {{"kind", "finish_surface"},
           {"target", {"model"}},
           {"tool", "finish"},
           {"pocketPattern", "parallel"},
           {"pocketAngle", 45}}}},
    };
    return spec.dump();
}

std::string layFlatAxisSwap(double extentX, double extentY, double extentZ) {
    // Top-down carving wants the shortest dimension pointing up. 'yz' makes
    // the Y extent the height; 'xz' makes the X extent the height.
    if (extentY < extentZ && extentY <= extentX)
        return "yz";
    if (extentX < extentZ && extentX < extentY)
        return "xz";
    return "none";
}

} // namespace dw::cam
