#include <gtest/gtest.h>

#include "core/carve/direct_carve_zeroing_probe.h"

TEST(DirectCarveZeroingProbe, ParsesSuccessfulProbeResult) {
    auto result = dw::carve::parseGrblProbeResult("[PRB:1.250,-2.500,0.000:1]");

    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->position.x, 1.25f);
    EXPECT_FLOAT_EQ(result->position.y, -2.5f);
    EXPECT_FLOAT_EQ(result->position.z, 0.0f);
    EXPECT_TRUE(result->contact);
}

TEST(DirectCarveZeroingProbe, ParsesFailedProbeResult) {
    auto result = dw::carve::parseGrblProbeResult("[PRB:10.000,20.000,-5.000:0]");

    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->position.x, 10.0f);
    EXPECT_FLOAT_EQ(result->position.y, 20.0f);
    EXPECT_FLOAT_EQ(result->position.z, -5.0f);
    EXPECT_FALSE(result->contact);
}

TEST(DirectCarveZeroingProbe, RejectsNonProbeLines) {
    EXPECT_FALSE(dw::carve::parseGrblProbeResult("ok").has_value());
    EXPECT_FALSE(dw::carve::parseGrblProbeResult("[MSG:hello]").has_value());
    EXPECT_FALSE(dw::carve::parseGrblProbeResult("[PRB:1,2:1]").has_value());
}

TEST(DirectCarveZeroingProbe, SienciAutoZeroDefaultsCapturePlateGeometry) {
    auto profile = dw::carve::defaultSienciAutoZeroProfile();

    EXPECT_FLOAT_EQ(profile.zPlateThicknessMm, 5.0f);
    EXPECT_FLOAT_EQ(profile.originOffsetMm, 22.5f);
    EXPECT_FLOAT_EQ(profile.finalZRetractMm, 1.0f);
    EXPECT_GT(profile.autoModeSpanMm, profile.tipModeSpanMm);
    EXPECT_GT(profile.lateralSearchMm, profile.autoModeApproachMm);
}
