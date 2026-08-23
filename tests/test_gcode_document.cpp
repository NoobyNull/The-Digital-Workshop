#include <gtest/gtest.h>

#include "core/carve/gcode_export.h"
#include "core/gcode/gcode_document.h"
#include "core/gcode/machine_profile.h"

namespace dw::gcode {
namespace {

TEST(GCodeDocument, PreservesExactTextAndPreparesSharedMetadata) {
    const std::string exactText =
        "(prepared once)\n"
        "G90 G21\n"
        "G0 X0 Y0 Z5\n"
        "G1 X10 Y2 Z-1 F600\n"
        "T3 M6\n";

    const auto document = prepareDocument(exactText, MachineProfile{});

    EXPECT_EQ(document.exactText, exactText);
    ASSERT_TRUE(document.hasCommands());
    ASSERT_TRUE(document.hasMotion());
    ASSERT_EQ(document.feedRates, std::vector<f32>({600.0f}));
    ASSERT_EQ(document.toolNumbers, std::vector<int>({3}));
    EXPECT_FLOAT_EQ(document.statistics.boundsMin.z, -1.0f);
    EXPECT_FLOAT_EQ(document.statistics.boundsMax.z, 5.0f);
    EXPECT_GT(document.statistics.totalPathLength, 0.0f);
    EXPECT_EQ(document.statistics.segmentTimes.size(), document.program.path.size());
}

TEST(GCodeDocument, CanonicalDirectCarveProgramRetainsThreeDimensionalMotion) {
    carve::MultiPassToolpath toolpath;
    toolpath.finishing.points = {
        {{2.0f, 3.0f, 5.0f}, true},
        {{2.0f, 3.0f, -2.0f}, false},
        {{12.0f, 3.0f, -1.0f}, false},
        {{12.0f, 3.0f, 5.0f}, true},
    };
    carve::ToolpathConfig config;
    config.safeZMm = 5.0f;
    config.feedRateMmMin = 700.0f;
    config.plungeRateMmMin = 200.0f;

    const auto exactText = carve::generateGcode(
        toolpath, config, "relief", "ball nose");
    const auto document = prepareDocument(exactText, MachineProfile{});

    EXPECT_EQ(document.exactText, exactText);
    ASSERT_TRUE(document.hasMotion());
    EXPECT_FLOAT_EQ(document.program.boundsMin.z, -2.0f);
    EXPECT_FLOAT_EQ(document.program.boundsMax.z, 5.0f);
    EXPECT_LT(document.program.boundsMin.z, document.program.boundsMax.z);
}

TEST(GCodeDocument, NormalizesInchProgramGeometryAndFeedsToMillimeters) {
    const auto document = prepareDocument(
        "G20\nG0 X1 Y2 Z0.5\nG1 X2 Y1 Z-0.25 F10\n",
        MachineProfile{});

    ASSERT_EQ(document.program.units, Units::Inches);
    ASSERT_EQ(document.program.path.size(), 2U);
    const auto& rapid = document.program.path[0];
    EXPECT_NEAR(rapid.end.x, 25.4f, 0.0001f);
    EXPECT_NEAR(rapid.end.y, 50.8f, 0.0001f);
    EXPECT_NEAR(rapid.end.z, 12.7f, 0.0001f);
    const auto& cut = document.program.path[1];
    EXPECT_NEAR(cut.end.x, 50.8f, 0.0001f);
    EXPECT_NEAR(cut.end.y, 25.4f, 0.0001f);
    EXPECT_NEAR(cut.end.z, -6.35f, 0.0001f);
    EXPECT_NEAR(cut.feedRate, 254.0f, 0.0001f);
}

} // namespace
} // namespace dw::gcode
