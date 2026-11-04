#include "ui/panels/cam_placeholder_panel.h"
#include <gtest/gtest.h>

namespace dw {

TEST(CamPlaceholderPanel, ReportsWorkflowStatusCopy) {
    CamPlaceholderPanel panel;
    const auto& copy = panel.statusCopy();
    EXPECT_NE(copy.find("CAM"), std::string::npos);
    EXPECT_NE(copy.find("Run"), std::string::npos);
}

TEST(CamPlaceholderPanel, DefaultsToFluidncMachine) {
    CamPlaceholderPanel panel;
    EXPECT_EQ(panel.selectedMachineId(), "fluidnc");
}

TEST(CamPlaceholderPanel, VisibilityDefaultsOpenAndToggles) {
    CamPlaceholderPanel panel;
    EXPECT_TRUE(panel.isOpen());
    panel.setOpen(false);
    EXPECT_FALSE(panel.isOpen());
}

TEST(CamPlaceholderPanel, ReportsEngineStatusFromProvider) {
    CamPlaceholderPanel panel;
    panel.setEngineStatusProvider(
        [] { return std::string("Engine ready at http://127.0.0.1:8973"); });
    EXPECT_EQ(panel.engineStatusLine(), "Engine ready at http://127.0.0.1:8973");
}

TEST(CamPlaceholderPanel, EngineStatusDefaultsToNotStarted) {
    CamPlaceholderPanel panel;
    EXPECT_EQ(panel.engineStatusLine(), "Engine not started");
}

} // namespace dw
