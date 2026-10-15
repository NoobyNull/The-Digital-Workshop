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
    const auto scale100 = chooseSideColumn(1366.0F, 13.0F, kSettingsSidebar);
    const auto scale150 = chooseSideColumn(1366.0F, 19.5F, kSettingsSidebar);
    const auto scale200 = chooseSideColumn(1366.0F, 26.0F, kSettingsSidebar);

    EXPECT_FLOAT_EQ(scale100.sideWidth, 338.0F);
    EXPECT_GT(scale150.sideWidth, scale100.sideWidth);
    EXPECT_GT(scale200.sideWidth, scale150.sideWidth);
    EXPECT_FLOAT_EQ(scale200.sideWidth, 520.0F);
    EXPECT_GE(scale200.centerWidth, 480.0F);
    EXPECT_NEAR(scale200.sideWidth + scale200.centerWidth, 1366.0F, 0.01F);
}

TEST(WorkshopLayoutMetrics, FourKColumnsStayNearContentWidth) {
    const auto sidebar = chooseSideColumn(3840.0F, 13.0F, kSettingsSidebar);
    const auto machine = chooseSideColumn(3840.0F, 13.0F, kMachineControlColumn);

    EXPECT_FLOAT_EQ(sidebar.sideWidth, 338.0F);
    EXPECT_FLOAT_EQ(machine.sideWidth, 364.0F);
    EXPECT_LT(sidebar.splitRatio, 0.10F);
    EXPECT_LT(machine.splitRatio, 0.10F);
    EXPECT_GT(sidebar.centerWidth, 3000.0F);
}

TEST(WorkshopLayoutMetrics, NarrowWorkAreasKeepAUsableCenter) {
    for (const auto spec : {kSettingsSidebar, kMachineControlColumn, kLibraryBrowser}) {
        const auto layout = chooseSideColumn(800.0F, 26.0F, spec);

        EXPECT_NEAR(layout.centerWidth, 480.0F, 0.01F);
        EXPECT_NEAR(layout.sideWidth, 320.0F, 0.01F);
        EXPECT_GE(layout.splitRatio, 0.0F);
        EXPECT_LE(layout.splitRatio, 1.0F);
    }

    const auto tiny = chooseSideColumn(300.0F, 13.0F, kSettingsSidebar);
    EXPECT_FLOAT_EQ(tiny.sideWidth, 0.0F);
    EXPECT_FLOAT_EQ(tiny.centerWidth, 300.0F);
}

TEST(WorkshopLayoutMetrics, LibraryBrowserTakesTheLargerShare) {
    const auto library = chooseSideColumn(1600.0F, 16.0F, kLibraryBrowser);
    const auto sidebar = chooseSideColumn(1600.0F, 16.0F, kSettingsSidebar);

    EXPECT_FLOAT_EQ(library.sideWidth, 768.0F);
    EXPECT_GT(library.sideWidth, sidebar.sideWidth);
    EXPECT_GE(library.centerWidth, 480.0F);
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

    for (const char* token : {"chooseSideColumn", "kSettingsSidebar",
                              "kMachineControlColumn", "kLibraryBrowser", ".splitRatio"}) {
        EXPECT_NE(dock.find(token), std::string::npos) << token;
    }
    EXPECT_NE(home.find("chooseHomeLayout"), std::string::npos);
    EXPECT_NE(home.find("##HomeContent"), std::string::npos);
    EXPECT_NE(home.find("homeLayout.horizontalOffset"), std::string::npos);
}

} // namespace
} // namespace dw::workshop::ui
