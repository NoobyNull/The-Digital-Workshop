#include "cam_job_spec.h"

#include <nlohmann/json.hpp>

namespace dw::cam {
namespace {

// Fallback tooling when the tool library offers nothing usable; values match
// cambridge/bridge/examples/dome-fluidnc.json.
EngineTool fallbackRoughingTool() {
    EngineTool tool;
    tool.id = "rough";
    tool.name = "6mm flat (fallback)";
    tool.type = "flat_endmill";
    tool.diameter = 6;
    tool.flutes = 2;
    tool.rpm = 18000;
    tool.feed = 1500;
    tool.plungeFeed = 500;
    tool.stepdown = 2;
    tool.stepover = 0.45;
    return tool;
}

EngineTool fallbackFinishingTool() {
    EngineTool tool;
    tool.id = "finish";
    tool.name = "3mm ball (fallback)";
    tool.type = "ball_endmill";
    tool.diameter = 3;
    tool.flutes = 2;
    tool.rpm = 18000;
    tool.feed = 1200;
    tool.plungeFeed = 400;
    tool.stepdown = 1;
    tool.stepover = 0.12;
    return tool;
}

nlohmann::json toolJson(EngineTool tool, const char* roleId) {
    tool.id = roleId; // operations reference tools by role id
    nlohmann::json json = {
        {"id", tool.id},       {"name", tool.name},
        {"type", tool.type},   {"diameter", tool.diameter},
        {"flutes", tool.flutes},
    };
    if (tool.type == "v_bit")
        json["vBitAngle"] = tool.vBitAngle;
    if (tool.rpm > 0)
        json["rpm"] = tool.rpm;
    if (tool.feed > 0)
        json["feed"] = tool.feed;
    if (tool.plungeFeed > 0)
        json["plungeFeed"] = tool.plungeFeed;
    if (tool.stepdown > 0)
        json["stepdown"] = tool.stepdown;
    if (tool.stepover > 0)
        json["stepover"] = tool.stepover;
    return json;
}

} // namespace

std::string buildDefaultSurfacingJobSpec(const CamJobRequest& request) {
    const EngineTool rough = request.roughingTool.value_or(fallbackRoughingTool());
    const EngineTool finish = request.finishingTool.value_or(fallbackFinishingTool());

    nlohmann::json spec = {
        {"name", request.modelName},
        {"units", "mm"},
        {"machine", request.machineId},
        {"stock", "auto"},
        {"stockMargin", 2},
        {"tools", {toolJson(rough, "rough"), toolJson(finish, "finish")}},
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
