#include <gtest/gtest.h>

#include "core/gcode/machine_profile.h"
#include "core/gcode/machine_rigidity.h"

#include <cmath>
#include <limits>

using namespace dw;
using namespace dw::gcode;

TEST(MachineRigidity, CustomDriveIsAppendedWithoutChangingPersistedOrdinals) {
    EXPECT_EQ(static_cast<int>(DriveSystem::Belt), 0);
    EXPECT_EQ(static_cast<int>(DriveSystem::Acme), 1);
    EXPECT_EQ(static_cast<int>(DriveSystem::LeadScrew), 2);
    EXPECT_EQ(static_cast<int>(DriveSystem::BallScrew), 3);
    EXPECT_EQ(static_cast<int>(DriveSystem::Custom), 4);
}

TEST(MachineRigidity, ProvidesCanonicalDisplayNamesAndBuiltInFactors) {
    EXPECT_STREQ(driveSystemDisplayName(DriveSystem::Belt), "Belt");
    EXPECT_STREQ(driveSystemDisplayName(DriveSystem::Acme), "Acme");
    EXPECT_STREQ(driveSystemDisplayName(DriveSystem::LeadScrew), "Lead Screw");
    EXPECT_STREQ(driveSystemDisplayName(DriveSystem::BallScrew), "Ball Screw");
    EXPECT_STREQ(driveSystemDisplayName(DriveSystem::Custom), "Custom");

    EXPECT_DOUBLE_EQ(defaultRigidityFactor(DriveSystem::Belt), 0.80);
    EXPECT_DOUBLE_EQ(defaultRigidityFactor(DriveSystem::Acme), 0.90);
    EXPECT_DOUBLE_EQ(defaultRigidityFactor(DriveSystem::LeadScrew), 0.90);
    EXPECT_DOUBLE_EQ(defaultRigidityFactor(DriveSystem::BallScrew), 1.00);
    EXPECT_DOUBLE_EQ(defaultRigidityFactor(DriveSystem::Custom), 0.80);
}

TEST(MachineRigidity, NormalizesCustomInputToConservativeRange) {
    EXPECT_DOUBLE_EQ(normalizeRigidityFactor(0.73), 0.73);
    EXPECT_DOUBLE_EQ(normalizeRigidityFactor(0.0), kMinimumRigidityFactor);
    EXPECT_DOUBLE_EQ(normalizeRigidityFactor(-5.0), kMinimumRigidityFactor);
    EXPECT_DOUBLE_EQ(normalizeRigidityFactor(1.25), kMaximumRigidityFactor);
    EXPECT_DOUBLE_EQ(normalizeRigidityFactor(std::numeric_limits<f64>::quiet_NaN()),
                     kSafeRigidityFactor);
    EXPECT_DOUBLE_EQ(normalizeRigidityFactor(std::numeric_limits<f64>::infinity()),
                     kSafeRigidityFactor);
}

TEST(MachineRigidity, EffectiveFactorUsesCustomValueOnlyForCustomDrive) {
    MachineProfile profile;
    profile.customRigidityFactor = 0.73f;

    profile.driveSystem = DriveSystem::LeadScrew;
    EXPECT_DOUBLE_EQ(effectiveRigidityFactor(profile), 0.90);

    profile.driveSystem = DriveSystem::Custom;
    EXPECT_NEAR(effectiveRigidityFactor(profile), 0.73, 1e-6);
}

TEST(MachineRigidity, CustomProfileJsonRoundTripsDriveAndFactor) {
    MachineProfile original;
    original.name = "Double helical gear drive";
    original.driveSystem = DriveSystem::Custom;
    original.customRigidityFactor = 0.93f;

    const auto restored = MachineProfile::fromJsonString(original.toJsonString());

    EXPECT_EQ(restored.driveSystem, DriveSystem::Custom);
    EXPECT_FLOAT_EQ(restored.customRigidityFactor, 0.93f);
}

TEST(MachineRigidity, LegacyJsonKeepsSafeDefaultFactor) {
    const auto restored = MachineProfile::fromJsonString(
        R"({"name":"Legacy","driveSystem":"BallScrew"})");

    EXPECT_EQ(restored.driveSystem, DriveSystem::BallScrew);
    EXPECT_FLOAT_EQ(restored.customRigidityFactor, 0.80f);
    EXPECT_DOUBLE_EQ(effectiveRigidityFactor(restored), 1.00);
}

TEST(MachineRigidity, InvalidJsonFactorsAreSafe) {
    const auto high = MachineProfile::fromJsonString(
        R"({"driveSystem":"Custom","customRigidityFactor":4.0})");
    const auto low = MachineProfile::fromJsonString(
        R"({"driveSystem":"Custom","customRigidityFactor":0.0})");
    const auto wrongType = MachineProfile::fromJsonString(
        R"({"driveSystem":"Custom","customRigidityFactor":"rigid"})");

    EXPECT_FLOAT_EQ(high.customRigidityFactor, 1.0f);
    EXPECT_FLOAT_EQ(low.customRigidityFactor, 0.10f);
    EXPECT_FLOAT_EQ(wrongType.customRigidityFactor, 0.80f);
}

TEST(MachineRigidity, NonFiniteFactorSerializesAsSafeValue) {
    MachineProfile original;
    original.driveSystem = DriveSystem::Custom;
    original.customRigidityFactor = std::numeric_limits<f32>::quiet_NaN();

    const auto restored = MachineProfile::fromJsonString(original.toJsonString());

    EXPECT_FLOAT_EQ(restored.customRigidityFactor, 0.80f);
}
