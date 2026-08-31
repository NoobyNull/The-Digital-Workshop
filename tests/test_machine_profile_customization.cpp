#include <gtest/gtest.h>

#include "core/gcode/machine_profile_customization.h"

using namespace dw::gcode;

TEST(MachineProfileCustomization, BuiltInPresetBecomesPersistableCustomProfile) {
    MachineProfile preset = MachineProfile::foxalien8040();
    MachineProfile edited = preset;
    edited.driveSystem = DriveSystem::Custom;
    edited.customRigidityFactor = 0.92f;

    const auto copy = makeEditableMachineProfileCopy(preset, edited, {preset});

    EXPECT_FALSE(copy.builtIn);
    EXPECT_EQ(copy.name, preset.name + " (Custom)");
    EXPECT_EQ(copy.driveSystem, DriveSystem::Custom);
    EXPECT_FLOAT_EQ(copy.customRigidityFactor, 0.92f);
}

TEST(MachineProfileCustomization, PreservesUniqueUserName) {
    MachineProfile preset = MachineProfile::foxalien8040();
    MachineProfile edited = preset;
    edited.name = "Matthew's double-helical drive";

    const auto copy = makeEditableMachineProfileCopy(preset, edited, {preset});

    EXPECT_EQ(copy.name, edited.name);
    EXPECT_FALSE(copy.builtIn);
}

TEST(MachineProfileCustomization, AvoidsNamesThatWouldBeDroppedOnReload) {
    MachineProfile preset = MachineProfile::foxalien8040();
    MachineProfile first = preset;
    first.name = preset.name + " (Custom)";
    first.builtIn = false;
    MachineProfile second = first;
    second.name += " 2";

    const auto copy = makeEditableMachineProfileCopy(
        preset, preset, {preset, first, second});

    EXPECT_EQ(copy.name, preset.name + " (Custom) 3");
}
