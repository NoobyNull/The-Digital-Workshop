#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(MachineProfileUiArchitecture, CustomDriveExplainsAndEditsRigidityAsPercent) {
    const auto dialog = readFile(
        std::filesystem::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "dialogs" /
        "machine_profile_dialog.cpp");

    EXPECT_NE(dialog.find("\"Custom\""), std::string::npos);
    EXPECT_NE(dialog.find("Rigidity factor"), std::string::npos);
    EXPECT_NE(dialog.find("suggested feed and stepdown"), std::string::npos);
    EXPECT_NE(dialog.find("physical stiffness measurement"), std::string::npos);
    EXPECT_NE(dialog.find("effectiveRigidityFactor"), std::string::npos);
}

TEST(MachineProfileUiArchitecture, SavingBuiltInCreatesAndActivatesDurableCopy) {
    const auto dialog = readFile(
        std::filesystem::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "dialogs" /
        "machine_profile_dialog.cpp");

    EXPECT_NE(dialog.find("makeEditableMachineProfileCopy"), std::string::npos);
    EXPECT_NE(dialog.find("config.addMachineProfile(customProfile)"), std::string::npos);
    EXPECT_NE(dialog.find("config.setActiveMachineProfileIndex(m_editProfileIndex)"),
              std::string::npos);
    EXPECT_NE(dialog.find("Custom profile saved and set active"), std::string::npos);
}

TEST(MachineProfileUiArchitecture, ProfileCalculatorsShareOneAdapter) {
    const auto root = std::filesystem::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto direct = readFile(root / "direct_carve_panel.cpp");
    const auto browser = readFile(root / "tool_browser_panel.cpp");
    const auto cncTool = readFile(root / "cnc_tool_panel.cpp");

    EXPECT_NE(direct.find("applyMachineProfileToCalcInput(mp, ci)"), std::string::npos);
    EXPECT_NE(browser.find("applyMachineProfileToCalcInput(profile, input)"),
              std::string::npos);
    EXPECT_NE(cncTool.find("applyMachineProfileToCalcInput(mp, input)"),
              std::string::npos);

    EXPECT_EQ(direct.find("switch (mp.driveSystem)"), std::string::npos);
    EXPECT_EQ(browser.find("driveTypeFromProfile"), std::string::npos);
    EXPECT_EQ(cncTool.find("switch (mp.driveSystem)"), std::string::npos);
}
