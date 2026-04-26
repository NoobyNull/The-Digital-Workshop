// Digital Workshop - Tool Browser panel state tests

#include <gtest/gtest.h>

#include "ui/panels/tool_browser_panel.h"
#include "ui/tool_library_access.h"

TEST(ToolBrowserPanel, SelectionNeedsReloadWhenSelectedGeometryChanges) {
    EXPECT_FALSE(dw::toolBrowserSelectionNeedsReload("", ""));
    EXPECT_FALSE(dw::toolBrowserSelectionNeedsReload("geom-a", "geom-a"));
    EXPECT_TRUE(dw::toolBrowserSelectionNeedsReload("", "geom-a"));
    EXPECT_TRUE(dw::toolBrowserSelectionNeedsReload("geom-a", "geom-b"));
}

TEST(ToolBrowserPanel, DefaultMachineProfileRequiresConfiguration) {
    auto profile = dw::gcode::MachineProfile::defaultProfile();

    EXPECT_FALSE(dw::toolBrowserMachineProfileConfigured(profile));
    EXPECT_EQ(dw::toolBrowserMachineProfileLabel(profile), "Machine not configured");
}

TEST(ToolBrowserPanel, ConfiguredMachineProfileShowsAsCanonicalMachine) {
    auto profile = dw::gcode::MachineProfile::foxalien8040();

    EXPECT_TRUE(dw::toolBrowserMachineProfileConfigured(profile));
    EXPECT_EQ(dw::toolBrowserMachineProfileLabel(profile), "FoxAlien 8040");
}

TEST(ToolBrowserPanel, MachineProfileWithoutSpindlePowerRequiresConfiguration) {
    auto profile = dw::gcode::MachineProfile::shapeoko4();
    profile.spindlePower = 0.0f;

    EXPECT_FALSE(dw::toolBrowserMachineProfileConfigured(profile));
    EXPECT_EQ(dw::toolBrowserMachineProfileLabel(profile), "Machine not configured");
}

TEST(ToolLibraryAccess, UsesClearSharedEntryLabels) {
    EXPECT_STREQ(dw::kToolLibraryWindowTitle, "Tool Library");
    EXPECT_STREQ(dw::kToolLibraryMenuLabel, "Tool Library");
    EXPECT_STREQ(dw::kToolLibraryStatusButtonLabel, "Tools");
}

TEST(ToolLibraryAccess, StatusButtonIsVisibleWhenIdle) {
    EXPECT_TRUE(dw::toolLibraryStatusButtonVisible(false, false));
    EXPECT_FALSE(dw::toolLibraryStatusButtonVisible(true, false));
    EXPECT_FALSE(dw::toolLibraryStatusButtonVisible(false, true));
}
