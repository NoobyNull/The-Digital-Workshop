#include <gtest/gtest.h>

#include "core/carve/direct_carve_operation_state.h"

TEST(DirectCarveOperationState, ParsesSetupIntentFromOpenItem) {
    dw::ProjectOpenItem item;
    item.itemType = dw::ProjectOpenItemType::Operation;
    item.sourceKey = "direct_carve:relief";
    item.intentJson = R"({
        "operation_kind":"direct_carve",
        "model_name":"relief",
        "model_source_path":"/tmp/relief.stl",
        "material_id":42,
        "material_name":"Walnut",
        "stock":{"width_mm":300.0,"height_mm":200.0,"thickness_mm":19.0},
        "fit":{"scale":0.75,"offset_x_mm":12.5,"offset_y_mm":8.0,"depth_mm":4.5},
        "toolpath":{
            "scan_axis":"x_then_y",
            "mill_direction":"climb",
            "stepover_preset":"fine",
            "custom_stepover_pct":3.5,
            "safe_z_mm":7.0,
            "feed_rate_mm_min":1450.0,
            "plunge_rate_mm_min":350.0,
            "rapid_rate_mm_min":4200.0,
            "lead_in_mm":1.5,
            "scan_resolution_mm":0.25
        }
    })";
    item.snapshotJson = R"({
        "finishing_tool":{
            "id":"tool-finish",
            "name":"1/8 Ball",
            "type":"ball_nose",
            "units":"metric",
            "diameter_mm":3.175,
            "included_angle_deg":0.0,
            "flat_diameter":0.0,
            "tip_radius":1.5875,
            "flutes":2
        },
        "clearing_tool":{
            "id":"tool-clear",
            "name":"1/4 End Mill",
            "type":"end_mill",
            "units":"imperial",
            "diameter_mm":6.35,
            "included_angle_deg":0.0,
            "flat_diameter":0.25,
            "tip_radius":0.0,
            "flutes":2
        }
    })";

    auto parsed = dw::carve::parseDirectCarveOperationSetup(item);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->modelName, "relief");
    EXPECT_EQ(parsed->modelSourcePath, dw::Path("/tmp/relief.stl"));
    ASSERT_TRUE(parsed->materialId.has_value());
    EXPECT_EQ(*parsed->materialId, 42);
    EXPECT_EQ(parsed->materialName, "Walnut");
    EXPECT_FLOAT_EQ(parsed->stock.width, 300.0f);
    EXPECT_FLOAT_EQ(parsed->stock.height, 200.0f);
    EXPECT_FLOAT_EQ(parsed->stock.thickness, 19.0f);
    EXPECT_FLOAT_EQ(parsed->fit.scale, 0.75f);
    EXPECT_FLOAT_EQ(parsed->fit.offsetX, 12.5f);
    EXPECT_FLOAT_EQ(parsed->fit.offsetY, 8.0f);
    EXPECT_FLOAT_EQ(parsed->fit.depthMm, 4.5f);
    EXPECT_EQ(parsed->toolpath.axis, dw::carve::ScanAxis::XThenY);
    EXPECT_EQ(parsed->toolpath.direction, dw::carve::MillDirection::Climb);
    EXPECT_EQ(parsed->toolpath.stepoverPreset, dw::carve::StepoverPreset::Fine);
    EXPECT_FLOAT_EQ(parsed->toolpath.customStepoverPct, 3.5f);
    EXPECT_FLOAT_EQ(parsed->toolpath.safeZMm, 7.0f);
    EXPECT_FLOAT_EQ(parsed->toolpath.feedRateMmMin, 1450.0f);
    EXPECT_FLOAT_EQ(parsed->toolpath.plungeRateMmMin, 350.0f);
    EXPECT_FLOAT_EQ(parsed->toolpath.rapidRateMmMin, 4200.0f);
    EXPECT_FLOAT_EQ(parsed->toolpath.leadInMm, 1.5f);
    EXPECT_FLOAT_EQ(parsed->toolpath.scanResolutionMm, 0.25f);
    ASSERT_TRUE(parsed->finishingTool.has_value());
    EXPECT_EQ(parsed->finishingTool->id, "tool-finish");
    EXPECT_EQ(parsed->finishingTool->name_format, "1/8 Ball");
    EXPECT_EQ(parsed->finishingTool->tool_type, dw::VtdbToolType::BallNose);
    EXPECT_EQ(parsed->finishingTool->units, dw::VtdbUnits::Metric);
    EXPECT_DOUBLE_EQ(parsed->finishingTool->diameter, 3.175);
    EXPECT_EQ(parsed->finishingTool->num_flutes, 2);
    ASSERT_TRUE(parsed->clearingTool.has_value());
    EXPECT_EQ(parsed->clearingTool->tool_type, dw::VtdbToolType::EndMill);
    EXPECT_EQ(parsed->clearingTool->units, dw::VtdbUnits::Imperial);
}

TEST(DirectCarveOperationState, RejectsNonDirectCarveOperation) {
    dw::ProjectOpenItem item;
    item.itemType = dw::ProjectOpenItemType::Gcode;
    item.intentJson = R"({"operation_kind":"direct_carve"})";

    EXPECT_FALSE(dw::carve::parseDirectCarveOperationSetup(item).has_value());
}

TEST(DirectCarveOperationState, UsesMachineSnapshotRapidRateWhenToolpathOmitsIt) {
    dw::ProjectOpenItem item;
    item.itemType = dw::ProjectOpenItemType::Operation;
    item.intentJson = R"({
        "operation_kind":"direct_carve",
        "toolpath":{
            "feed_rate_mm_min":1200.0,
            "plunge_rate_mm_min":300.0
        }
    })";
    item.snapshotJson = R"({
        "machine":{
            "name":"Legacy snapshot",
            "rapid_rate_mm_min":3600.0
        }
    })";

    auto parsed = dw::carve::parseDirectCarveOperationSetup(item);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_FLOAT_EQ(parsed->toolpath.rapidRateMmMin, 3600.0f);
}

TEST(DirectCarveOperationState, BuildsAndParsesSienciAutoZeroItem) {
    dw::carve::DirectCarveZeroingSetup setup;
    setup.touchPlate = dw::carve::DirectCarveTouchPlate::SienciAutoZero;
    setup.probeMode = dw::carve::DirectCarveZeroProbeMode::XYZAuto;
    setup.bitMode = dw::carve::DirectCarveAutoZeroBitMode::Tip;
    setup.corner = dw::carve::DirectCarveZeroCorner::FrontLeft;
    setup.zPlateThicknessMm = 12.7f;
    setup.xyWallThicknessMm = 6.35f;
    setup.fastProbeMmMin = 150.0f;
    setup.slowProbeMmMin = 75.0f;
    setup.searchDistanceMm = 30.0f;
    setup.retractMm = 2.0f;
    setup.autoZeroOriginOffsetMm = 22.5f;
    setup.autoZeroFinalZRetractMm = 1.0f;
    setup.toolDiameterMm = 3.175f;
    setup.zeroVerified = true;

    auto item = dw::carve::makeDirectCarveZeroingOpenItem(
        55, "direct_carve:relief", setup);

    EXPECT_EQ(item.itemType, dw::ProjectOpenItemType::Zeroing);
    EXPECT_EQ(item.parentItemId, std::optional<dw::i64>(55));
    EXPECT_EQ(item.sourceKey, "direct_carve:relief:zeroing");
    EXPECT_EQ(item.status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(item.displayName, "Zeroing: Sienci AutoZero");

    auto parsed = dw::carve::parseDirectCarveZeroingSetup(item);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->touchPlate, dw::carve::DirectCarveTouchPlate::SienciAutoZero);
    EXPECT_EQ(parsed->probeMode, dw::carve::DirectCarveZeroProbeMode::XYZAuto);
    EXPECT_EQ(parsed->bitMode, dw::carve::DirectCarveAutoZeroBitMode::Tip);
    EXPECT_EQ(parsed->corner, dw::carve::DirectCarveZeroCorner::FrontLeft);
    EXPECT_FLOAT_EQ(parsed->zPlateThicknessMm, 12.7f);
    EXPECT_FLOAT_EQ(parsed->xyWallThicknessMm, 6.35f);
    EXPECT_FLOAT_EQ(parsed->autoZeroOriginOffsetMm, 22.5f);
    EXPECT_FLOAT_EQ(parsed->autoZeroFinalZRetractMm, 1.0f);
    EXPECT_FLOAT_EQ(parsed->toolDiameterMm, 3.175f);
    EXPECT_TRUE(parsed->zeroVerified);
}

TEST(DirectCarveOperationState, RejectsNonZeroingOpenItem) {
    dw::ProjectOpenItem item;
    item.itemType = dw::ProjectOpenItemType::Operation;
    item.intentJson = R"({"setup_kind":"direct_carve_zeroing"})";

    EXPECT_FALSE(dw::carve::parseDirectCarveZeroingSetup(item).has_value());
}
