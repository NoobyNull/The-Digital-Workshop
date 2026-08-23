#include "direct_carve_operation_state.h"

#include <nlohmann/json.hpp>

namespace dw {
namespace carve {

namespace {

ScanAxis parseScanAxis(const std::string& value) {
    if (value == "y_only")
        return ScanAxis::YOnly;
    if (value == "x_then_y")
        return ScanAxis::XThenY;
    if (value == "y_then_x")
        return ScanAxis::YThenX;
    return ScanAxis::XOnly;
}

CutExtents parseCutExtents(const std::string& value) {
    if (value == "material" || value == "material_extents") {
        return CutExtents::Material;
    }
    return CutExtents::Model;
}

MillDirection parseMillDirection(const std::string& value) {
    if (value == "climb")
        return MillDirection::Climb;
    if (value == "conventional")
        return MillDirection::Conventional;
    return MillDirection::Alternating;
}

StepoverPreset parseStepoverPreset(const std::string& value) {
    if (value == "ultra_fine")
        return StepoverPreset::UltraFine;
    if (value == "fine")
        return StepoverPreset::Fine;
    if (value == "rough")
        return StepoverPreset::Rough;
    if (value == "roughing")
        return StepoverPreset::Roughing;
    return StepoverPreset::Basic;
}

std::string touchPlateLabel(DirectCarveTouchPlate touchPlate) {
    switch (touchPlate) {
    case DirectCarveTouchPlate::SienciAutoZero:
        return "Sienci AutoZero";
    case DirectCarveTouchPlate::Generic:
        return "Generic Touch Plate";
    }
    return "Touch Plate";
}

const char* touchPlateKey(DirectCarveTouchPlate touchPlate) {
    switch (touchPlate) {
    case DirectCarveTouchPlate::SienciAutoZero:
        return "sienci_autozero";
    case DirectCarveTouchPlate::Generic:
        return "generic";
    }
    return "generic";
}

DirectCarveTouchPlate parseTouchPlate(const std::string& value) {
    if (value == "sienci_autozero") {
        return DirectCarveTouchPlate::SienciAutoZero;
    }
    return DirectCarveTouchPlate::Generic;
}

const char* zeroProbeModeKey(DirectCarveZeroProbeMode mode) {
    switch (mode) {
    case DirectCarveZeroProbeMode::XOnly:
        return "x_only";
    case DirectCarveZeroProbeMode::YOnly:
        return "y_only";
    case DirectCarveZeroProbeMode::XYCorner:
        return "xy_corner";
    case DirectCarveZeroProbeMode::XYZAuto:
        return "xyz_auto";
    case DirectCarveZeroProbeMode::ZOnly:
        return "z_only";
    }
    return "z_only";
}

DirectCarveZeroProbeMode parseZeroProbeMode(const std::string& value) {
    if (value == "x_only")
        return DirectCarveZeroProbeMode::XOnly;
    if (value == "y_only")
        return DirectCarveZeroProbeMode::YOnly;
    if (value == "xy_corner")
        return DirectCarveZeroProbeMode::XYCorner;
    if (value == "xyz_auto")
        return DirectCarveZeroProbeMode::XYZAuto;
    return DirectCarveZeroProbeMode::ZOnly;
}

const char* autoZeroBitModeKey(DirectCarveAutoZeroBitMode mode) {
    switch (mode) {
    case DirectCarveAutoZeroBitMode::Tip:
        return "tip";
    case DirectCarveAutoZeroBitMode::Auto:
        return "auto";
    }
    return "auto";
}

DirectCarveAutoZeroBitMode parseAutoZeroBitMode(const std::string& value) {
    if (value == "tip")
        return DirectCarveAutoZeroBitMode::Tip;
    return DirectCarveAutoZeroBitMode::Auto;
}

const char* zeroCornerKey(DirectCarveZeroCorner corner) {
    switch (corner) {
    case DirectCarveZeroCorner::FrontRight:
        return "front_right";
    case DirectCarveZeroCorner::BackRight:
        return "back_right";
    case DirectCarveZeroCorner::BackLeft:
        return "back_left";
    case DirectCarveZeroCorner::FrontLeft:
        return "front_left";
    }
    return "front_left";
}

DirectCarveZeroCorner parseZeroCorner(const std::string& value) {
    if (value == "front_right")
        return DirectCarveZeroCorner::FrontRight;
    if (value == "back_right")
        return DirectCarveZeroCorner::BackRight;
    if (value == "back_left")
        return DirectCarveZeroCorner::BackLeft;
    return DirectCarveZeroCorner::FrontLeft;
}

VtdbToolType parseToolType(const std::string& value) {
    if (value == "ball_nose")
        return VtdbToolType::BallNose;
    if (value == "radiused")
        return VtdbToolType::Radiused;
    if (value == "v_bit")
        return VtdbToolType::VBit;
    if (value == "tapered_ball_nose")
        return VtdbToolType::TaperedBallNose;
    if (value == "drill")
        return VtdbToolType::Drill;
    if (value == "thread_mill")
        return VtdbToolType::ThreadMill;
    if (value == "form_tool")
        return VtdbToolType::FormTool;
    if (value == "diamond_drag")
        return VtdbToolType::DiamondDrag;
    return VtdbToolType::EndMill;
}

std::optional<VtdbToolGeometry> parseToolSummary(const nlohmann::json& json) {
    if (!json.is_object() || json.empty()) {
        return std::nullopt;
    }

    VtdbToolGeometry tool;
    tool.id = json.value("id", std::string());
    tool.name_format = json.value("name", std::string());
    tool.tool_type = parseToolType(json.value("type", std::string()));
    tool.units = json.value("units", std::string()) == "imperial" ? VtdbUnits::Imperial
                                                                  : VtdbUnits::Metric;
    const f64 diameterMm = json.value("diameter_mm", 0.0);
    tool.diameter = tool.units == VtdbUnits::Imperial ? diameterMm / 25.4 : diameterMm;
    tool.included_angle = json.value("included_angle_deg", 0.0);
    tool.flat_diameter = json.value("flat_diameter", 0.0);
    tool.tip_radius = json.value("tip_radius", 0.0);
    tool.num_flutes = json.value("flutes", tool.num_flutes);
    return tool;
}

std::optional<VtdbToolGeometry> parseToolSummaryAt(const nlohmann::json& snapshot,
                                                   const char* key) {
    const auto found = snapshot.find(key);
    if (found == snapshot.end()) {
        return std::nullopt;
    }
    return parseToolSummary(*found);
}

f32 jsonF32(const nlohmann::json& json, const char* key, f32 fallback) {
    if (!json.contains(key) || !json[key].is_number()) {
        return fallback;
    }
    return json[key].get<f32>();
}

} // namespace

std::optional<DirectCarveOperationSetup> parseDirectCarveOperationSetup(
    const ProjectOpenItem& item) {
    if (item.itemType != ProjectOpenItemType::Operation) {
        return std::nullopt;
    }

    auto intent = nlohmann::json::parse(item.intentJson, nullptr, false);
    if (!intent.is_object() || intent.value("operation_kind", std::string()) != "direct_carve") {
        return std::nullopt;
    }

    DirectCarveOperationSetup setup;
    setup.modelName = intent.value("model_name", std::string());
    setup.modelSourcePath = intent.value("model_source_path", std::string());
    if (setup.modelName.empty() && setup.modelSourcePath.empty())
        return std::nullopt;
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
    setup.toolpath.cutExtents = parseCutExtents(toolpath.value("cut_extents", std::string()));
    setup.toolpath.direction = parseMillDirection(toolpath.value("mill_direction", std::string()));
    setup.toolpath.stepoverPreset =
        parseStepoverPreset(toolpath.value("stepover_preset", std::string()));
    setup.toolpath.customStepoverPct =
        jsonF32(toolpath, "custom_stepover_pct", setup.toolpath.customStepoverPct);
    setup.toolpath.safeZMm = jsonF32(toolpath, "safe_z_mm", setup.toolpath.safeZMm);
    setup.toolpath.feedRateMmMin =
        jsonF32(toolpath, "feed_rate_mm_min", setup.toolpath.feedRateMmMin);
    setup.toolpath.plungeRateMmMin =
        jsonF32(toolpath, "plunge_rate_mm_min", setup.toolpath.plungeRateMmMin);
    setup.toolpath.rapidRateMmMin =
        jsonF32(toolpath, "rapid_rate_mm_min", setup.toolpath.rapidRateMmMin);
    setup.toolpath.stepdownMm = jsonF32(toolpath, "stepdown_mm", setup.toolpath.stepdownMm);
    setup.toolpath.leadInMm = jsonF32(toolpath, "lead_in_mm", setup.toolpath.leadInMm);
    setup.toolpath.scanResolutionMm =
        jsonF32(toolpath, "scan_resolution_mm", setup.toolpath.scanResolutionMm);

    auto snapshot = nlohmann::json::parse(item.snapshotJson, nullptr, false);
    if (snapshot.is_object()) {
        if (!toolpath.contains("rapid_rate_mm_min")) {
            const auto machine = snapshot.value("machine", nlohmann::json::object());
            setup.toolpath.rapidRateMmMin =
                jsonF32(machine, "rapid_rate_mm_min", setup.toolpath.rapidRateMmMin);
        }
        setup.finishingTool = parseToolSummaryAt(snapshot, "finishing_tool");

        const auto mode = snapshot.find("clearing_mode");
        const bool hasExplicitMode = mode != snapshot.end() && mode->is_string();
        if (hasExplicitMode) {
            setup.clearingToolMode = parseClearingToolModeKey(mode->get_ref<const std::string&>());
            setup.selectedClearingTool = parseToolSummaryAt(snapshot, "selected_clearing_tool");
            setup.effectiveClearingTool = parseToolSummaryAt(snapshot, "effective_clearing_tool");

            // Disabled means no clearing pass can be effective. Keep the
            // selected intent so switching back to Selected can restore it.
            if (setup.clearingToolMode == ClearingToolMode::Disabled) {
                setup.effectiveClearingTool.reset();
            }
        } else {
            // Legacy snapshots had one ambiguous key. Preserve the exact tool
            // as explicit intent, but do not claim it generated a nonempty
            // clearing pass because the old format could not prove that.
            setup.selectedClearingTool = parseToolSummaryAt(snapshot, "clearing_tool");
            setup.clearingToolMode = setup.selectedClearingTool ? ClearingToolMode::Selected
                                                                : ClearingToolMode::Automatic;
        }
    }

    return setup;
}

ProjectOpenItem makeDirectCarveZeroingOpenItem(i64 operationItemId,
                                               const std::string& operationSourceKey,
                                               const DirectCarveZeroingSetup& setup) {
    nlohmann::json intent = {
        {"setup_kind", "direct_carve_zeroing"},
        {"operation_source_key", operationSourceKey},
        {"touch_plate", touchPlateKey(setup.touchPlate)},
        {"probe_mode", zeroProbeModeKey(setup.probeMode)},
        {"bit_mode", autoZeroBitModeKey(setup.bitMode)},
        {"corner", zeroCornerKey(setup.corner)},
        {"work_coordinate_system", "G54"},
    };

    nlohmann::json snapshot = {
        {"zero_verified", setup.zeroVerified},
        {"probe",
         {
             {"z_plate_thickness_mm", setup.zPlateThicknessMm},
             {"xy_wall_thickness_mm", setup.xyWallThicknessMm},
             {"fast_probe_mm_min", setup.fastProbeMmMin},
             {"slow_probe_mm_min", setup.slowProbeMmMin},
             {"search_distance_mm", setup.searchDistanceMm},
             {"retract_mm", setup.retractMm},
             {"autozero_origin_offset_mm", setup.autoZeroOriginOffsetMm},
             {"autozero_final_z_retract_mm", setup.autoZeroFinalZRetractMm},
             {"tool_diameter_mm", setup.toolDiameterMm},
         }},
    };

    if (setup.touchPlate == DirectCarveTouchPlate::SienciAutoZero) {
        snapshot["sienci_autozero"] = {
            {"source_url", "https://sienci.com/product/autozero/"},
            {"supports_xyz", true},
            {"supports_auto_tool_size", true},
            {"supports_tip_mode", true},
            {"documented_corner", "front_left"},
        };
    }

    ProjectOpenItem item;
    item.itemType = ProjectOpenItemType::Zeroing;
    item.sourceTable = "direct_carve";
    item.sourceKey = operationSourceKey + ":zeroing";
    item.parentItemId = operationItemId;
    item.status = setup.zeroVerified ? ProjectOpenItemStatus::Ready
                                     : ProjectOpenItemStatus::Planned;
    item.displayName = "Zeroing: " + touchPlateLabel(setup.touchPlate);
    item.intentJson = intent.dump();
    item.snapshotJson = snapshot.dump();
    return item;
}

std::optional<DirectCarveZeroingSetup> parseDirectCarveZeroingSetup(const ProjectOpenItem& item) {
    if (item.itemType != ProjectOpenItemType::Zeroing) {
        return std::nullopt;
    }

    auto intent = nlohmann::json::parse(item.intentJson, nullptr, false);
    if (!intent.is_object() ||
        intent.value("setup_kind", std::string()) != "direct_carve_zeroing") {
        return std::nullopt;
    }

    auto snapshot = nlohmann::json::parse(item.snapshotJson, nullptr, false);
    if (!snapshot.is_object()) {
        snapshot = nlohmann::json::object();
    }

    DirectCarveZeroingSetup setup;
    setup.touchPlate = parseTouchPlate(intent.value("touch_plate", std::string()));
    setup.probeMode = parseZeroProbeMode(intent.value("probe_mode", std::string()));
    setup.bitMode = parseAutoZeroBitMode(intent.value("bit_mode", std::string()));
    setup.corner = parseZeroCorner(intent.value("corner", std::string()));
    setup.zeroVerified = snapshot.value("zero_verified", false);

    const auto probe = snapshot.value("probe", nlohmann::json::object());
    setup.zPlateThicknessMm = jsonF32(probe, "z_plate_thickness_mm", setup.zPlateThicknessMm);
    setup.xyWallThicknessMm = jsonF32(probe, "xy_wall_thickness_mm", setup.xyWallThicknessMm);
    setup.fastProbeMmMin = jsonF32(probe, "fast_probe_mm_min", setup.fastProbeMmMin);
    setup.slowProbeMmMin = jsonF32(probe, "slow_probe_mm_min", setup.slowProbeMmMin);
    setup.searchDistanceMm = jsonF32(probe, "search_distance_mm", setup.searchDistanceMm);
    setup.retractMm = jsonF32(probe, "retract_mm", setup.retractMm);
    setup.autoZeroOriginOffsetMm =
        jsonF32(probe, "autozero_origin_offset_mm", setup.autoZeroOriginOffsetMm);
    setup.autoZeroFinalZRetractMm =
        jsonF32(probe, "autozero_final_z_retract_mm", setup.autoZeroFinalZRetractMm);
    setup.toolDiameterMm = jsonF32(probe, "tool_diameter_mm", setup.toolDiameterMm);
    return setup;
}

} // namespace carve
} // namespace dw
