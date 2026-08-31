// Digital Workshop - ToolCalculator Tests

#include <gtest/gtest.h>

#include "core/cnc/tool_calculator.h"

#include <limits>

using namespace dw;

// ============================================================================
// Material Classification
// ============================================================================

TEST(ToolCalculator, Classify_SoftWood) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(380.0, "White Pine"), HardnessBand::Soft);
    EXPECT_EQ(ToolCalculator::classifyMaterial(799.0, ""), HardnessBand::Soft);
}

TEST(ToolCalculator, Classify_MediumWood) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(800.0, ""), HardnessBand::Medium);
    EXPECT_EQ(ToolCalculator::classifyMaterial(1290.0, "Red Oak"), HardnessBand::Medium);
    EXPECT_EQ(ToolCalculator::classifyMaterial(1499.0, ""), HardnessBand::Medium);
}

TEST(ToolCalculator, Classify_HardWood) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(1500.0, ""), HardnessBand::Hard);
    EXPECT_EQ(ToolCalculator::classifyMaterial(1820.0, "Hickory"), HardnessBand::Hard);
    EXPECT_EQ(ToolCalculator::classifyMaterial(2499.0, ""), HardnessBand::Hard);
}

TEST(ToolCalculator, Classify_VeryHardWood) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(2500.0, ""), HardnessBand::VeryHard);
    EXPECT_EQ(ToolCalculator::classifyMaterial(3680.0, "Ipe"), HardnessBand::VeryHard);
}

TEST(ToolCalculator, Classify_Composite) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "MDF"), HardnessBand::Composite);
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "Baltic Birch Plywood"), HardnessBand::Composite);
}

TEST(ToolCalculator, Classify_Metal) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "Aluminum (6061)"), HardnessBand::Metal);
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "Brass"), HardnessBand::Metal);
}

TEST(ToolCalculator, Classify_Plastic) {
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "HDPE"), HardnessBand::Plastic);
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "Acrylic"), HardnessBand::Plastic);
    EXPECT_EQ(ToolCalculator::classifyMaterial(0.0, "Rigid Foam (PVC)"), HardnessBand::Plastic);
}

// ============================================================================
// Rigidity Factor
// ============================================================================

TEST(ToolCalculator, Rigidity_Belt) {
    EXPECT_DOUBLE_EQ(ToolCalculator::rigidityFactor(DriveType::Belt), 0.80);
}

TEST(ToolCalculator, Rigidity_LeadScrew) {
    EXPECT_DOUBLE_EQ(ToolCalculator::rigidityFactor(DriveType::LeadScrew), 0.90);
}

TEST(ToolCalculator, Rigidity_BallScrew) {
    EXPECT_DOUBLE_EQ(ToolCalculator::rigidityFactor(DriveType::BallScrew), 1.00);
}

TEST(ToolCalculator, Rigidity_RackPinion) {
    EXPECT_DOUBLE_EQ(ToolCalculator::rigidityFactor(DriveType::RackPinion), 1.00);
}

// ============================================================================
// RPM Calculation
// ============================================================================

TEST(ToolCalculator, RPM_QuarterInchEndMill) {
    // SFM=500, dia=0.25" -> RPM = (500*12)/(pi*0.25) ≈ 7639
    int rpm = ToolCalculator::calculateRPM(500.0, 0.25, 24000);
    EXPECT_GT(rpm, 7000);
    EXPECT_LT(rpm, 8000);
}

TEST(ToolCalculator, RPM_ClampedToMax) {
    // Very small diameter should produce high RPM, clamped to max
    int rpm = ToolCalculator::calculateRPM(500.0, 0.0625, 24000);
    EXPECT_EQ(rpm, 24000);
}

TEST(ToolCalculator, RPM_ZeroDiameter) {
    EXPECT_EQ(ToolCalculator::calculateRPM(500.0, 0.0, 24000), 0);
}

// ============================================================================
// Chip Load
// ============================================================================

TEST(ToolCalculator, ChipLoad_QuarterInchMedium) {
    double cl = ToolCalculator::chipLoad(HardnessBand::Medium, 0.25, 2);
    EXPECT_GT(cl, 0.003);
    EXPECT_LT(cl, 0.008);
}

TEST(ToolCalculator, ChipLoad_SoftHigher) {
    double clSoft = ToolCalculator::chipLoad(HardnessBand::Soft, 0.25, 2);
    double clMedium = ToolCalculator::chipLoad(HardnessBand::Medium, 0.25, 2);
    EXPECT_GT(clSoft, clMedium);
}

TEST(ToolCalculator, ChipLoad_MetalLower) {
    double clMetal = ToolCalculator::chipLoad(HardnessBand::Metal, 0.25, 2);
    double clMedium = ToolCalculator::chipLoad(HardnessBand::Medium, 0.25, 2);
    EXPECT_LT(clMetal, clMedium);
}

TEST(ToolCalculator, ChipLoad_MoreFlutesReducePerTooth) {
    double cl2 = ToolCalculator::chipLoad(HardnessBand::Medium, 0.25, 2);
    double cl3 = ToolCalculator::chipLoad(HardnessBand::Medium, 0.25, 3);
    EXPECT_LT(cl3, cl2);
}

TEST(ToolCalculator, ChipLoad_ZeroDiameter) {
    EXPECT_DOUBLE_EQ(ToolCalculator::chipLoad(HardnessBand::Medium, 0.0, 2), 0.0);
}

TEST(ToolCalculator, ChipLoad_ZeroFlutes) {
    EXPECT_DOUBLE_EQ(ToolCalculator::chipLoad(HardnessBand::Medium, 0.25, 0), 0.0);
}

// ============================================================================
// Full Calculation
// ============================================================================

TEST(ToolCalculator, Calculate_QuarterInchEndMill_RedOak) {
    CalcInput input;
    input.diameter = 0.25;
    input.num_flutes = 2;
    input.tool_type = VtdbToolType::EndMill;
    input.units = VtdbUnits::Imperial;
    input.janka_hardness = 1290.0; // Red Oak
    input.max_rpm = 24000;
    input.drive_type = DriveType::Belt;

    auto result = ToolCalculator::calculate(input);

    EXPECT_EQ(result.hardness_band, HardnessBand::Medium);
    EXPECT_DOUBLE_EQ(result.rigidity_factor, 0.80);
    EXPECT_GT(result.rpm, 0);
    EXPECT_LE(result.rpm, 24000);
    EXPECT_GT(result.feed_rate, 0.0);
    EXPECT_GT(result.plunge_rate, 0.0);
    EXPECT_GT(result.stepdown, 0.0);
    EXPECT_GT(result.stepover, 0.0);
    EXPECT_GT(result.chip_load, 0.0);
    // Plunge should be ~50% of feed for wood
    EXPECT_NEAR(result.plunge_rate, result.feed_rate * 0.5, 0.01);
}

TEST(ToolCalculator, Calculate_WoodUsesConfiguredRouterSpindleRPM) {
    CalcInput input;
    input.diameter = 0.375;
    input.num_flutes = 2;
    input.tool_type = VtdbToolType::BallNose;
    input.units = VtdbUnits::Imperial;
    input.janka_hardness = 1450.0; // Hard Maple, currently classified as medium wood
    input.material_name = "Hard Maple";
    input.max_rpm = 24000;
    input.drive_type = DriveType::Belt;
    input.spindle_power_watts = 1500.0;

    auto result = ToolCalculator::calculate(input);

    EXPECT_EQ(result.hardness_band, HardnessBand::Medium);
    EXPECT_EQ(result.rpm, 24000);
    EXPECT_NEAR(result.feed_rate, 230.4, 0.1);
    EXPECT_NEAR(result.plunge_rate, 115.2, 0.1);
}

TEST(ToolCalculator, Calculate_BallScrewHigherFeed) {
    CalcInput base;
    base.diameter = 0.25;
    base.num_flutes = 2;
    base.janka_hardness = 1290.0;
    base.max_rpm = 24000;

    base.drive_type = DriveType::Belt;
    auto beltResult = ToolCalculator::calculate(base);

    base.drive_type = DriveType::BallScrew;
    auto screwResult = ToolCalculator::calculate(base);

    // BallScrew should give 25% higher feed than Belt (1.0 vs 0.8)
    EXPECT_GT(screwResult.feed_rate, beltResult.feed_rate);
    EXPECT_NEAR(screwResult.feed_rate / beltResult.feed_rate, 1.25, 0.01);
}

TEST(ToolCalculator, Calculate_ExplicitRigidityOverrideScalesFeedAndStepdown) {
    CalcInput base;
    base.diameter = 0.25;
    base.num_flutes = 2;
    base.janka_hardness = 1290.0;
    base.max_rpm = 24000;
    base.drive_type = DriveType::BallScrew;

    const auto fullRigidity = ToolCalculator::calculate(base);
    base.rigidity_factor_override = 0.73;
    const auto customRigidity = ToolCalculator::calculate(base);

    EXPECT_DOUBLE_EQ(customRigidity.rigidity_factor, 0.73);
    EXPECT_NEAR(customRigidity.feed_rate / fullRigidity.feed_rate, 0.73, 1e-9);
    EXPECT_NEAR(customRigidity.stepdown / fullRigidity.stepdown, 0.73, 1e-9);
}

TEST(ToolCalculator, Calculate_ExplicitRigidityOverrideIsNormalizedSafely) {
    CalcInput input;
    input.diameter = 0.25;
    input.num_flutes = 2;
    input.janka_hardness = 1290.0;
    input.max_rpm = 24000;
    input.drive_type = DriveType::BallScrew;

    input.rigidity_factor_override = 5.0;
    EXPECT_DOUBLE_EQ(ToolCalculator::calculate(input).rigidity_factor, 1.0);

    input.rigidity_factor_override = 0.0;
    EXPECT_DOUBLE_EQ(ToolCalculator::calculate(input).rigidity_factor, 0.10);

    input.rigidity_factor_override = std::numeric_limits<f64>::quiet_NaN();
    EXPECT_DOUBLE_EQ(ToolCalculator::calculate(input).rigidity_factor, 0.80);
}

TEST(ToolCalculator, Calculate_MetalLowerPlungeRatio) {
    CalcInput input;
    input.diameter = 0.25;
    input.num_flutes = 2;
    input.janka_hardness = 0.0;
    input.material_name = "Aluminum (6061)";
    input.max_rpm = 24000;
    input.drive_type = DriveType::BallScrew;

    auto result = ToolCalculator::calculate(input);

    EXPECT_EQ(result.hardness_band, HardnessBand::Metal);
    // Plunge should be 30% of feed for metal
    EXPECT_NEAR(result.plunge_rate, result.feed_rate * 0.30, 0.01);
}

TEST(ToolCalculator, Calculate_PowerLimiting) {
    CalcInput input;
    input.diameter = 0.5;
    input.num_flutes = 2;
    input.janka_hardness = 1290.0;
    input.max_rpm = 24000;
    input.drive_type = DriveType::BallScrew;
    input.spindle_power_watts = 1.0; // Unrealistically low to force power limiting

    auto result = ToolCalculator::calculate(input);

    EXPECT_TRUE(result.power_limited);
    EXPECT_LE(result.power_required, 1.0 + 0.01); // Should fit within power budget
}

TEST(ToolCalculator, Calculate_NoPowerLimit_WhenZeroWatts) {
    CalcInput input;
    input.diameter = 0.25;
    input.num_flutes = 2;
    input.janka_hardness = 1290.0;
    input.max_rpm = 24000;
    input.drive_type = DriveType::Belt;
    input.spindle_power_watts = 0.0; // No power limit specified

    auto result = ToolCalculator::calculate(input);

    EXPECT_FALSE(result.power_limited);
}

TEST(ToolCalculator, Calculate_ZeroDiameter_ReturnsEmpty) {
    CalcInput input;
    input.diameter = 0.0;
    input.num_flutes = 2;

    auto result = ToolCalculator::calculate(input);

    EXPECT_EQ(result.rpm, 0);
    EXPECT_DOUBLE_EQ(result.feed_rate, 0.0);
}

TEST(ToolCalculator, Calculate_Metric_ConvertedCorrectly) {
    CalcInput imperial;
    imperial.diameter = 0.25;
    imperial.num_flutes = 2;
    imperial.janka_hardness = 1290.0;
    imperial.max_rpm = 24000;
    imperial.drive_type = DriveType::Belt;
    imperial.units = VtdbUnits::Imperial;

    CalcInput metric;
    metric.diameter = 0.25 * 25.4; // 6.35mm
    metric.num_flutes = 2;
    metric.janka_hardness = 1290.0;
    metric.max_rpm = 24000;
    metric.drive_type = DriveType::Belt;
    metric.units = VtdbUnits::Metric;

    auto impResult = ToolCalculator::calculate(imperial);
    auto metResult = ToolCalculator::calculate(metric);

    // RPM should be identical (same physical tool)
    EXPECT_EQ(impResult.rpm, metResult.rpm);
    // Feed rates should be proportional (mm = in * 25.4)
    EXPECT_NEAR(metResult.feed_rate, impResult.feed_rate * 25.4, 0.5);
}

TEST(ToolCalculator, Calculate_SoftWood_HigherFeedThanHard) {
    CalcInput base;
    base.diameter = 0.25;
    base.num_flutes = 2;
    base.max_rpm = 24000;
    base.drive_type = DriveType::BallScrew;

    base.janka_hardness = 380.0; // Pine
    auto softResult = ToolCalculator::calculate(base);

    base.janka_hardness = 1820.0; // Hickory
    auto hardResult = ToolCalculator::calculate(base);

    EXPECT_GT(softResult.feed_rate, hardResult.feed_rate);
    EXPECT_GT(softResult.stepdown, hardResult.stepdown);
}

// ============================================================================
// SFM Recommendations
// ============================================================================

TEST(ToolCalculator, SFM_DecreasesWithHardness) {
    auto sfmSoft = ToolCalculator::recommendedSFM(HardnessBand::Soft, VtdbToolType::EndMill);
    auto sfmMed = ToolCalculator::recommendedSFM(HardnessBand::Medium, VtdbToolType::EndMill);
    auto sfmHard = ToolCalculator::recommendedSFM(HardnessBand::Hard, VtdbToolType::EndMill);
    auto sfmVH = ToolCalculator::recommendedSFM(HardnessBand::VeryHard, VtdbToolType::EndMill);

    EXPECT_GT(sfmSoft, sfmMed);
    EXPECT_GT(sfmMed, sfmHard);
    EXPECT_GT(sfmHard, sfmVH);
}

TEST(ToolCalculator, SFM_BallNoseLowerThanEndMill) {
    auto sfmEM = ToolCalculator::recommendedSFM(HardnessBand::Medium, VtdbToolType::EndMill);
    auto sfmBN = ToolCalculator::recommendedSFM(HardnessBand::Medium, VtdbToolType::BallNose);
    EXPECT_LT(sfmBN, sfmEM);
}
