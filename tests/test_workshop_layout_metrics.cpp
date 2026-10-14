#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "modules/workshop/ui/workshop_layout_metrics.h"

namespace dw::workshop::ui {
namespace {

std::string readSource(const char* relativePath) {
    std::ifstream input(std::string(CMAKE_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

TEST(WorkshopLayoutMetrics, ScaledSidebarsGrowWhilePreservingTheCenter) {
    const auto scale100 = chooseWorkshopDockLayout(1366.0F, 13.0F);
    const auto scale150 = chooseWorkshopDockLayout(1366.0F, 19.5F);
    const auto scale200 = chooseWorkshopDockLayout(1366.0F, 26.0F);

    EXPECT_FLOAT_EQ(scale100.leftWidth, 260.0F);
    EXPECT_GT(scale150.leftWidth, 340.0F);
    EXPECT_GT(scale200.leftWidth, 420.0F);
    EXPECT_GT(scale150.leftWidth, scale100.leftWidth);
    EXPECT_GT(scale200.leftWidth, scale150.leftWidth);
    EXPECT_GE(scale200.centerWidth, 480.0F);
    EXPECT_NEAR(scale200.leftWidth + scale200.centerWidth + scale200.rightWidth,
                1366.0F,
                0.01F);
}

TEST(WorkshopLayoutMetrics, FourKSidebarsStayNearContentWidth) {
    const auto layout = chooseWorkshopDockLayout(3840.0F, 13.0F);

    EXPECT_FLOAT_EQ(layout.leftWidth, 260.0F);
    EXPECT_FLOAT_EQ(layout.rightWidth, 220.0F);
    EXPECT_LT(layout.leftSplitRatio, 0.10F);
    EXPECT_LT(layout.rightSplitRatio, 0.10F);
    EXPECT_GT(layout.centerWidth, 3000.0F);
}

TEST(WorkshopLayoutMetrics, NarrowWorkAreasKeepAUsableCenter) {
    const auto layout = chooseWorkshopDockLayout(800.0F, 26.0F);

    EXPECT_NEAR(layout.centerWidth, 480.0F, 0.01F);
    EXPECT_GE(layout.leftWidth, 0.0F);
    EXPECT_GE(layout.rightWidth, 0.0F);
    EXPECT_GE(layout.leftSplitRatio, 0.0F);
    EXPECT_LE(layout.leftSplitRatio, 1.0F);
    EXPECT_GE(layout.rightSplitRatio, 0.0F);
    EXPECT_LE(layout.rightSplitRatio, 1.0F);
}

TEST(WorkshopLayoutMetrics, HomeRetainsCompactWidthAndCentersWideContent) {
    const auto compact = chooseHomeLayout(1346.0F, 8.0F);
    EXPECT_FLOAT_EQ(compact.contentWidth, 1346.0F);
    EXPECT_FLOAT_EQ(compact.horizontalOffset, 0.0F);

    const auto fourK = chooseHomeLayout(3820.0F, 8.0F);
    EXPECT_FLOAT_EQ(fourK.contentWidth, 1440.0F);
    EXPECT_FLOAT_EQ(fourK.horizontalOffset, 1190.0F);
    EXPECT_LT(fourK.rightColumnWidth, 700.0F);
    EXPECT_NEAR(fourK.leftColumnWidth + fourK.rightColumnWidth + 8.0F,
                fourK.contentWidth,
                0.01F);
}

TEST(WorkshopLayoutMetrics, RuntimePathsUseTheSharedPolicy) {
    const auto dock = readSource("src/managers/ui_manager_layout.cpp");
    const auto home = readSource("src/ui/panels/start_page.cpp");

    EXPECT_NE(dock.find("chooseWorkshopDockLayout"), std::string::npos);
    EXPECT_NE(dock.find("dockLayout.leftSplitRatio"), std::string::npos);
    EXPECT_NE(dock.find("dockLayout.rightSplitRatio"), std::string::npos);
    EXPECT_NE(home.find("chooseHomeLayout"), std::string::npos);
    EXPECT_NE(home.find("##HomeContent"), std::string::npos);
    EXPECT_NE(home.find("homeLayout.horizontalOffset"), std::string::npos);
}

} // namespace
} // namespace dw::workshop::ui
