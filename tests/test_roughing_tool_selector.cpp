#include "core/carve/roughing_tool_selector.h"

#include <gtest/gtest.h>

using namespace dw;
using namespace dw::carve;

namespace {

VtdbToolGeometry makeTool(std::string id,
                          VtdbToolType type,
                          f64 diameter,
                          VtdbUnits units = VtdbUnits::Metric)
{
    VtdbToolGeometry tool;
    tool.id = std::move(id);
    tool.name_format = tool.id;
    tool.tool_type = type;
    tool.units = units;
    tool.diameter = diameter;
    tool.num_flutes = 2;
    return tool;
}

} // namespace

TEST(RoughingToolSelector, PicksLargestFlatToolThatCanClearOutsideModel)
{
    const auto finish = makeTool("1/32 TBN", VtdbToolType::TaperedBallNose,
                                 0.03125, VtdbUnits::Imperial);
    const std::vector<VtdbToolGeometry> toolbox = {
        makeTool("small-endmill", VtdbToolType::EndMill, 3.0),
        makeTool("large-endmill", VtdbToolType::EndMill, 6.0),
        makeTool("vbit", VtdbToolType::VBit, 12.0),
    };

    const auto result = selectFixedDepthRoughingTool(
        toolbox,
        finish,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{60.0f, 40.0f, 0.0f},
        Vec3{20.0f, 10.0f, 0.0f},
        Vec3{40.0f, 30.0f, 0.0f});

    ASSERT_TRUE(result.tool.has_value());
    EXPECT_EQ(result.tool->id, "large-endmill");
    EXPECT_TRUE(result.requiresToolChange);
    EXPECT_TRUE(result.warning.empty());
}

TEST(RoughingToolSelector, SkipsDetailFinishToolWhenNoFlatRougherExists)
{
    const auto finish = makeTool("tiny-tbn", VtdbToolType::TaperedBallNose, 1.0);
    const std::vector<VtdbToolGeometry> toolbox = {
        makeTool("ball", VtdbToolType::BallNose, 6.0),
        makeTool("vbit", VtdbToolType::VBit, 12.0),
    };

    const auto result = selectFixedDepthRoughingTool(
        toolbox,
        finish,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{60.0f, 40.0f, 0.0f},
        Vec3{20.0f, 10.0f, 0.0f},
        Vec3{40.0f, 30.0f, 0.0f});

    EXPECT_FALSE(result.tool.has_value());
    EXPECT_FALSE(result.requiresToolChange);
    EXPECT_NE(result.warning.find("No suitable roughing tool"), std::string::npos);
}

TEST(RoughingToolSelector, ReusesLargeFlatFinishToolWhenNoLargerRougherExists)
{
    const auto finish = makeTool("6mm-endmill", VtdbToolType::EndMill, 6.0);
    const std::vector<VtdbToolGeometry> toolbox = {
        makeTool("small-endmill", VtdbToolType::EndMill, 3.0),
    };

    const auto result = selectFixedDepthRoughingTool(
        toolbox,
        finish,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{60.0f, 40.0f, 0.0f},
        Vec3{20.0f, 10.0f, 0.0f},
        Vec3{40.0f, 30.0f, 0.0f});

    ASSERT_TRUE(result.tool.has_value());
    EXPECT_EQ(result.tool->id, "6mm-endmill");
    EXPECT_FALSE(result.requiresToolChange);
}

TEST(RoughingToolSelector, MaterialExtentsCanRoughEvenWhenModelConsumesStock)
{
    const auto finish = makeTool("tiny-tbn", VtdbToolType::TaperedBallNose, 1.0);
    const std::vector<VtdbToolGeometry> toolbox = {
        makeTool("6mm-endmill", VtdbToolType::EndMill, 6.0),
    };

    const auto result = selectFixedDepthRoughingTool(
        toolbox,
        finish,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{60.0f, 40.0f, 0.0f},
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{60.0f, 40.0f, 0.0f},
        CutExtents::Material);

    ASSERT_TRUE(result.tool.has_value());
    EXPECT_EQ(result.tool->id, "6mm-endmill");
    EXPECT_TRUE(result.requiresToolChange);
}
