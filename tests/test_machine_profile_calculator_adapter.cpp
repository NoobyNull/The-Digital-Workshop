#include <gtest/gtest.h>

#include "core/cnc/machine_profile_calculator_adapter.h"

#include <limits>

using namespace dw;

TEST(MachineProfileCalculatorAdapter, MapsKnownDriveFamiliesToLegacyTypes) {
    EXPECT_EQ(calculatorDriveType(gcode::DriveSystem::Belt), DriveType::Belt);
    EXPECT_EQ(calculatorDriveType(gcode::DriveSystem::Acme), DriveType::LeadScrew);
    EXPECT_EQ(calculatorDriveType(gcode::DriveSystem::LeadScrew), DriveType::LeadScrew);
    EXPECT_EQ(calculatorDriveType(gcode::DriveSystem::BallScrew), DriveType::BallScrew);
    EXPECT_EQ(calculatorDriveType(gcode::DriveSystem::Custom), DriveType::Belt);
}

TEST(MachineProfileCalculatorAdapter, AppliesMachineInputsAndEffectiveRigidity) {
    gcode::MachineProfile profile;
    profile.spindlePower = 2200.0f;
    profile.spindleMaxRPM = 24000.0f;
    profile.driveSystem = gcode::DriveSystem::Custom;
    profile.customRigidityFactor = 0.93f;

    CalcInput input;
    input.diameter = 0.25;
    input.num_flutes = 3;
    input.janka_hardness = 1290.0;
    input.material_name = "Red Oak";

    applyMachineProfileToCalcInput(profile, input);

    EXPECT_DOUBLE_EQ(input.spindle_power_watts, 2200.0);
    EXPECT_EQ(input.max_rpm, 24000);
    EXPECT_EQ(input.drive_type, DriveType::Belt);
    ASSERT_TRUE(input.rigidity_factor_override.has_value());
    EXPECT_NEAR(*input.rigidity_factor_override, 0.93, 1e-6);

    EXPECT_DOUBLE_EQ(input.diameter, 0.25);
    EXPECT_EQ(input.num_flutes, 3);
    EXPECT_DOUBLE_EQ(input.janka_hardness, 1290.0);
    EXPECT_EQ(input.material_name, "Red Oak");
}

TEST(MachineProfileCalculatorAdapter, BuiltInDriveUsesCanonicalFactor) {
    gcode::MachineProfile profile;
    profile.driveSystem = gcode::DriveSystem::BallScrew;
    profile.customRigidityFactor = 0.25f;

    CalcInput input;
    applyMachineProfileToCalcInput(profile, input);

    EXPECT_EQ(input.drive_type, DriveType::BallScrew);
    ASSERT_TRUE(input.rigidity_factor_override.has_value());
    EXPECT_DOUBLE_EQ(*input.rigidity_factor_override, 1.0);
}

TEST(MachineProfileCalculatorAdapter, NormalizesCustomFactorThroughSharedPolicy) {
    gcode::MachineProfile profile;
    profile.driveSystem = gcode::DriveSystem::Custom;
    profile.customRigidityFactor = 5.0f;

    CalcInput input;
    applyMachineProfileToCalcInput(profile, input);

    ASSERT_TRUE(input.rigidity_factor_override.has_value());
    EXPECT_DOUBLE_EQ(*input.rigidity_factor_override, 1.0);

    profile.customRigidityFactor = std::numeric_limits<f32>::quiet_NaN();
    applyMachineProfileToCalcInput(profile, input);
    EXPECT_DOUBLE_EQ(*input.rigidity_factor_override, 0.80);
}

TEST(MachineProfileCalculatorAdapter, InvalidSpindleValuesFailSafe) {
    gcode::MachineProfile profile;
    profile.spindlePower = -500.0f;
    profile.spindleMaxRPM = std::numeric_limits<f32>::quiet_NaN();

    CalcInput input;
    applyMachineProfileToCalcInput(profile, input);

    EXPECT_DOUBLE_EQ(input.spindle_power_watts, 0.0);
    EXPECT_EQ(input.max_rpm, 0);
}
