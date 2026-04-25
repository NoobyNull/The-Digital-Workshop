#include "direct_carve_operation_state.h"

#include <nlohmann/json.hpp>

namespace dw {
namespace carve {

namespace {

ScanAxis parseScanAxis(const std::string& value) {
    if (value == "y_only") return ScanAxis::YOnly;
    if (value == "x_then_y") return ScanAxis::XThenY;
    if (value == "y_then_x") return ScanAxis::YThenX;
    return ScanAxis::XOnly;
}

MillDirection parseMillDirection(const std::string& value) {
    if (value == "climb") return MillDirection::Climb;
    if (value == "conventional") return MillDirection::Conventional;
    return MillDirection::Alternating;
}

StepoverPreset parseStepoverPreset(const std::string& value) {
    if (value == "ultra_fine") return StepoverPreset::UltraFine;
    if (value == "fine") return StepoverPreset::Fine;
    if (value == "rough") return StepoverPreset::Rough;
    if (value == "roughing") return StepoverPreset::Roughing;
    return StepoverPreset::Basic;
}

VtdbToolType parseToolType(const std::string& value) {
    if (value == "ball_nose") return VtdbToolType::BallNose;
    if (value == "radiused") return VtdbToolType::Radiused;
    if (value == "v_bit") return VtdbToolType::VBit;
    if (value == "tapered_ball_nose") return VtdbToolType::TaperedBallNose;
    if (value == "drill") return VtdbToolType::Drill;
    if (value == "thread_mill") return VtdbToolType::ThreadMill;
    if (value == "form_tool") return VtdbToolType::FormTool;
    if (value == "diamond_drag") return VtdbToolType::DiamondDrag;
    return VtdbToolType::EndMill;
}

std::optional<VtdbToolGeometry> parseToolSummary(const nlohmann::json& json) {
    if (!json.is_object()) {
        return std::nullopt;
    }

    VtdbToolGeometry tool;
    tool.id = json.value("id", std::string());
    tool.name_format = json.value("name", std::string());
    tool.tool_type = parseToolType(json.value("type", std::string()));
    tool.units = json.value("units", std::string()) == "imperial"
                     ? VtdbUnits::Imperial
                     : VtdbUnits::Metric;
    tool.diameter = json.value("diameter_mm", 0.0);
    tool.included_angle = json.value("included_angle_deg", 0.0);
    tool.flat_diameter = json.value("flat_diameter", 0.0);
    tool.tip_radius = json.value("tip_radius", 0.0);
    tool.num_flutes = json.value("flutes", tool.num_flutes);
    return tool;
}

f32 jsonF32(const nlohmann::json& json, const char* key, f32 fallback) {
    if (!json.contains(key) || !json[key].is_number()) {
        return fallback;
    }
    return json[key].get<f32>();
}

} // namespace

std::optional<DirectCarveOperationSetup>
parseDirectCarveOperationSetup(const ProjectOpenItem& item) {
    if (item.itemType != ProjectOpenItemType::Operation) {
        return std::nullopt;
    }

    auto intent = nlohmann::json::parse(item.intentJson, nullptr, false);
    if (!intent.is_object() ||
        intent.value("operation_kind", std::string()) != "direct_carve") {
        return std::nullopt;
    }

    DirectCarveOperationSetup setup;
    setup.modelName = intent.value("model_name", std::string());
    setup.modelSourcePath = intent.value("model_source_path", std::string());
    if (intent.contains("material_id") && intent["material_id"].is_number_integer()) {
        setup.materialId = intent["material_id"].get<i64>();
    }
    setup.materialName = intent.value("material_name", std::string());

    const auto stock = intent.value("stock", nlohmann::json::object());
    setup.stock.width = jsonF32(stock, "width_mm", setup.stock.width);
    setup.stock.height = jsonF32(stock, "height_mm", setup.stock.height);
    setup.stock.thickness = jsonF32(stock, "thickness_mm", setup.stock.thickness);

    const auto fit = intent.value("fit", nlohmann::json::object());
    setup.fit.scale = jsonF32(fit, "scale", setup.fit.scale);
    setup.fit.offsetX = jsonF32(fit, "offset_x_mm", setup.fit.offsetX);
    setup.fit.offsetY = jsonF32(fit, "offset_y_mm", setup.fit.offsetY);
    setup.fit.depthMm = jsonF32(fit, "depth_mm", setup.fit.depthMm);

    const auto toolpath = intent.value("toolpath", nlohmann::json::object());
    setup.toolpath.axis = parseScanAxis(toolpath.value("scan_axis", std::string()));
    setup.toolpath.direction =
        parseMillDirection(toolpath.value("mill_direction", std::string()));
    setup.toolpath.stepoverPreset =
        parseStepoverPreset(toolpath.value("stepover_preset", std::string()));
    setup.toolpath.customStepoverPct =
        jsonF32(toolpath, "custom_stepover_pct", setup.toolpath.customStepoverPct);
    setup.toolpath.safeZMm = jsonF32(toolpath, "safe_z_mm", setup.toolpath.safeZMm);
    setup.toolpath.feedRateMmMin =
        jsonF32(toolpath, "feed_rate_mm_min", setup.toolpath.feedRateMmMin);
    setup.toolpath.plungeRateMmMin =
        jsonF32(toolpath, "plunge_rate_mm_min", setup.toolpath.plungeRateMmMin);
    setup.toolpath.leadInMm = jsonF32(toolpath, "lead_in_mm", setup.toolpath.leadInMm);
    setup.toolpath.scanResolutionMm =
        jsonF32(toolpath, "scan_resolution_mm", setup.toolpath.scanResolutionMm);

    auto snapshot = nlohmann::json::parse(item.snapshotJson, nullptr, false);
    if (snapshot.is_object()) {
        setup.finishingTool = parseToolSummary(snapshot.value("finishing_tool",
                                                             nlohmann::json::object()));
        setup.clearingTool = parseToolSummary(snapshot.value("clearing_tool",
                                                            nlohmann::json::object()));
    }

    return setup;
}

} // namespace carve
} // namespace dw
