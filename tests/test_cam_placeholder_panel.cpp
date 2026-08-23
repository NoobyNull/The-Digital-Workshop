#include "ui/panels/cam_placeholder_panel.h"
#include <gtest/gtest.h>

namespace dw {

TEST(CamPlaceholderPanel, ReportsRebuildStatusCopy) {
    CamPlaceholderPanel panel;
    const auto& copy = panel.statusCopy();
    EXPECT_NE(copy.find("CAM"), std::string::npos);
    EXPECT_NE(copy.find("rebuilt"), std::string::npos);
}

TEST(CamPlaceholderPanel, VisibilityDefaultsClosedAndToggles) {
    CamPlaceholderPanel panel;
    EXPECT_FALSE(panel.isVisible());
    panel.setVisible(true);
    EXPECT_TRUE(panel.isVisible());
}

} // namespace dw
