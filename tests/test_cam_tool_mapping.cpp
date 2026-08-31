#include "core/cam/cam_tool_mapping.h"

#include <gtest/gtest.h>

namespace dw::cam {

TEST(CamToolMapping, ConvertsFeedRateUnitsToMmPerMin) {
    EXPECT_DOUBLE_EQ(feedToMmPerMin(10.0, 0), 600.0);    // mm/sec
    EXPECT_DOUBLE_EQ(feedToMmPerMin(1500.0, 1), 1500.0); // mm/min
    EXPECT_DOUBLE_EQ(feedToMmPerMin(1.5, 2), 1500.0);    // m/min
    EXPECT_DOUBLE_EQ(feedToMmPerMin(1.0, 3), 1524.0);    // in/sec
    EXPECT_DOUBLE_EQ(feedToMmPerMin(100.0, 4), 2540.0);  // in/min
}

TEST(CamToolMapping, ProjectsImperialEndMillToMetricEngineTool) {
    VtdbToolGeometry geometry;
    geometry.id = "g1";
    geometry.tool_type = VtdbToolType::EndMill;
    geometry.units = VtdbUnits::Imperial;
    geometry.diameter = 0.25; // inches
    geometry.num_flutes = 3;

    VtdbCuttingData cutting;
    cutting.rate_units = 4; // in/min
    cutting.feed_rate = 100.0;
    cutting.plunge_rate = 30.0;
    cutting.spindle_speed = 16000;
    cutting.length_units = 1; // imperial lengths
    cutting.stepdown = 0.1;   // inches
    cutting.stepover = 0.1;   // inches

    const auto tool = toEngineTool(geometry, &cutting);
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->type, "flat_endmill");
    EXPECT_DOUBLE_EQ(tool->diameter, 6.35);
    EXPECT_EQ(tool->flutes, 3);
    EXPECT_EQ(tool->rpm, 16000);
    EXPECT_DOUBLE_EQ(tool->feed, 2540.0);
    EXPECT_DOUBLE_EQ(tool->plungeFeed, 762.0);
    EXPECT_DOUBLE_EQ(tool->stepdown, 2.54);
    EXPECT_DOUBLE_EQ(tool->stepover, 0.4); // 2.54mm of 6.35mm diameter
}

TEST(CamToolMapping, MapsBallNoseAndVBit) {
    VtdbToolGeometry ball;
    ball.id = "b";
    ball.tool_type = VtdbToolType::BallNose;
    ball.units = VtdbUnits::Metric;
    ball.diameter = 3.0;
    const auto ballTool = toEngineTool(ball, nullptr);
    ASSERT_TRUE(ballTool.has_value());
    EXPECT_EQ(ballTool->type, "ball_endmill");

    VtdbToolGeometry vbit;
    vbit.id = "v";
    vbit.tool_type = VtdbToolType::VBit;
    vbit.units = VtdbUnits::Metric;
    vbit.diameter = 12.7;
    vbit.included_angle = 60.0;
    const auto vTool = toEngineTool(vbit, nullptr);
    ASSERT_TRUE(vTool.has_value());
    EXPECT_EQ(vTool->type, "v_bit");
    EXPECT_DOUBLE_EQ(vTool->vBitAngle, 60.0);
}

TEST(CamToolMapping, RejectsToolsWithoutEngineEquivalent) {
    VtdbToolGeometry laser;
    laser.id = "l";
    laser.tool_type = VtdbToolType::DiamondDrag;
    laser.units = VtdbUnits::Metric;
    laser.diameter = 1.0;
    EXPECT_FALSE(toEngineTool(laser, nullptr).has_value());

    VtdbToolGeometry zeroDiameter;
    zeroDiameter.id = "z";
    zeroDiameter.tool_type = VtdbToolType::EndMill;
    zeroDiameter.units = VtdbUnits::Metric;
    zeroDiameter.diameter = 0.0;
    EXPECT_FALSE(toEngineTool(zeroDiameter, nullptr).has_value());
}

} // namespace dw::cam
