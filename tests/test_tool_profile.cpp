// Digital Workshop - Tool cut-profile inference tests

#include <gtest/gtest.h>

#include <algorithm>

#include "core/cnc/tool_profile.h"

namespace {

dw::VtdbToolGeometry makeGeometry(int rawType) {
    dw::VtdbToolGeometry g;
    g.id = "profile-test";
    g.tool_type = static_cast<dw::VtdbToolType>(rawType);
    g.units = dw::VtdbUnits::Imperial;
    return g;
}

}  // namespace

TEST(ToolProfile, UnknownTypeWithAngleInfersVGrooveEngraving) {
    auto g = makeGeometry(4);
    g.diameter = 0.25;
    g.included_angle = 60.0;
    g.flat_diameter = 0.005;

    auto profile = dw::describeToolProfile(g);

    EXPECT_EQ(profile.shape, dw::ToolProfileShape::VGroove);
    EXPECT_EQ(profile.rawToolType, 4);
    EXPECT_FALSE(profile.typeKnown);
    EXPECT_TRUE(profile.needsMapping);
    EXPECT_EQ(profile.label, "V-Groove / Engraving");
    ASSERT_FALSE(profile.badges.empty());
    EXPECT_EQ(profile.badges[0], "Needs Mapping");
}

TEST(ToolProfile, LaserFieldsInferLaserProfileEvenForUnknownType) {
    auto g = makeGeometry(12);
    g.laser_watt = 3;

    auto profile = dw::describeToolProfile(g);

    EXPECT_EQ(profile.shape, dw::ToolProfileShape::Laser);
    EXPECT_FALSE(profile.typeKnown);
    EXPECT_TRUE(profile.needsMapping);
    EXPECT_EQ(profile.label, "Laser");
}

TEST(ToolProfile, KnownVBitDoesNotNeedMapping) {
    auto g = makeGeometry(static_cast<int>(dw::VtdbToolType::VBit));
    g.diameter = 0.25;
    g.included_angle = 90.0;

    auto profile = dw::describeToolProfile(g);

    EXPECT_EQ(profile.shape, dw::ToolProfileShape::VGroove);
    EXPECT_TRUE(profile.typeKnown);
    EXPECT_FALSE(profile.needsMapping);
    EXPECT_EQ(profile.label, "V-Bit");
}

TEST(ToolProfile, LargeFlatEndMillIsSurfacingEnvelope) {
    auto g = makeGeometry(static_cast<int>(dw::VtdbToolType::EndMill));
    g.units = dw::VtdbUnits::Imperial;
    g.diameter = 1.5;

    auto profile = dw::describeToolProfile(g);

    EXPECT_EQ(profile.shape, dw::ToolProfileShape::FlatEnd);
    EXPECT_EQ(profile.label, "Surfacing / Spoilboard Cutter");
    EXPECT_FALSE(profile.needsMapping);
}

TEST(ToolProfile, TaperedBallNoseWithMissingGeometryReportsMissingFields) {
    auto g = makeGeometry(static_cast<int>(dw::VtdbToolType::TaperedBallNose));

    auto profile = dw::describeToolProfile(g);

    EXPECT_EQ(profile.shape, dw::ToolProfileShape::TaperedBallNose);
    EXPECT_FALSE(profile.needsMapping);
    EXPECT_NE(std::find(profile.badges.begin(), profile.badges.end(), "Missing Diameter"),
              profile.badges.end());
    EXPECT_NE(std::find(profile.badges.begin(), profile.badges.end(), "Missing Tip Radius"),
              profile.badges.end());
    EXPECT_NE(std::find(profile.badges.begin(), profile.badges.end(), "Missing Angle"),
              profile.badges.end());
}

TEST(ToolProfile, PreviewDimensionsKeepTaperedBallNoseVisibleWhenRawValuesAreZero) {
    auto g = makeGeometry(static_cast<int>(dw::VtdbToolType::TaperedBallNose));
    auto profile = dw::describeToolProfile(g);

    EXPECT_GT(dw::toolProfilePreviewDiameterMm(g, profile), 0.0);
    EXPECT_GT(dw::toolProfilePreviewTipRadiusMm(g, profile), 0.0);
    EXPECT_LT(dw::toolProfilePreviewTipRadiusMm(g, profile) * 2.0,
              dw::toolProfilePreviewDiameterMm(g, profile));
    EXPECT_GT(dw::toolProfilePreviewCutHeightMm(g, profile), 0.0);
}

TEST(ToolProfile, ResolvesSpeToolTaperedBallNoseDimensionsFromDescription) {
    auto g = makeGeometry(static_cast<int>(dw::VtdbToolType::TaperedBallNose));
    g.units = dw::VtdbUnits::Metric;
    g.diameter = 3.175;
    g.included_angle = 10.24;
    g.tip_radius = 0.25;
    g.flute_length = 0.0;
    g.name_format =
        "SpeTool W01001 5Pcs CNC 2D and 3D Carving 5.12 Deg Tapered Angle "
        "Ball Nose 0.25mm Radius x 1/8\" Shank x 15mm Cutting Length x "
        "1-1/2\" Long 2 Flute SC TiAlN Coated Upcut Router Bits";

    const auto resolved = dw::resolveToolProfileGeometry(g);

    EXPECT_EQ(resolved.shape, dw::ToolProfileShape::TaperedBallNose);
    EXPECT_TRUE(resolved.sideAngleParsed);
    EXPECT_TRUE(resolved.cutHeightParsed);
    EXPECT_NEAR(resolved.sideAngleDeg, 5.12, 0.001);
    EXPECT_NEAR(resolved.tipRadiusMm, 0.25, 0.001);
    EXPECT_NEAR(resolved.cutHeightMm, 15.0, 0.001);
}

TEST(ToolProfile, ResolvesImperialMixedFractionCuttingLengthFromDescription) {
    auto g = makeGeometry(static_cast<int>(dw::VtdbToolType::EndMill));
    g.units = dw::VtdbUnits::Imperial;
    g.diameter = 0.375;
    g.flute_length = 0.0;
    g.name_format =
        "SpeTool W04034 SC Spiral Plunge 3/8\" Dia x 3/8\" Shank x "
        "1-1/4\" Cutting Length x 3\" Long 2 Flute Down-Cut Router Bit";

    const auto resolved = dw::resolveToolProfileGeometry(g);

    EXPECT_EQ(resolved.shape, dw::ToolProfileShape::FlatEnd);
    EXPECT_TRUE(resolved.cutHeightParsed);
    EXPECT_NEAR(resolved.diameterMm, 9.525, 0.001);
    EXPECT_NEAR(resolved.cutHeightMm, 31.75, 0.001);
}
