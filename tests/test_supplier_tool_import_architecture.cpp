#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t lineCount(const fs::path& path) {
    const auto contents = readFile(path);
    return static_cast<std::size_t>(
        std::count(contents.begin(), contents.end(), '\n'));
}

} // namespace

TEST(SupplierToolImportArchitecture, ToolBrowserOnlyChoosesSourceAndDelegates) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto panel = readFile(root / "ui" / "panels" / "tool_browser_panel.cpp");

    EXPECT_NE(panel.find("\"Import Tools...\""), std::string::npos);
    EXPECT_NE(panel.find("showNativeOpen"), std::string::npos);
    EXPECT_NE(panel.find("m_supplierToolImportDialog->openSource"), std::string::npos);
    EXPECT_EQ(panel.find("SelectiveToolImporter::copySelected"), std::string::npos);
    EXPECT_EQ(panel.find("importFromVtdb(Path(path))"), std::string::npos);
}

TEST(SupplierToolImportArchitecture, GlobalDialogOwnsSelectionAndCopyTask) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto dialogPath = root / "ui" / "dialogs" / "supplier_tool_import_dialog.cpp";
    const auto browserPath =
        root / "ui" / "dialogs" / "supplier_tool_import_dialog_browser.cpp";
    const auto dialog = readFile(dialogPath);
    const auto browser = readFile(browserPath);
    const auto manager = readFile(root / "managers" / "ui_manager.cpp");
    const auto wiring = readFile(root / "app" / "application_wiring_cnc.cpp");

    EXPECT_LE(lineCount(dialogPath), 500u);
    EXPECT_LE(lineCount(browserPath), 500u);
    EXPECT_NE(dialog.find("SelectiveToolImporter::copySelected"), std::string::npos);
    EXPECT_NE(dialog.find("Select all shown"), std::string::npos);
    EXPECT_NE(dialog.find("In library"), std::string::npos);
    EXPECT_NE(dialog.find("Also add copied tools to My Toolbox"), std::string::npos);
    EXPECT_NE(manager.find("m_supplierToolImportDialog.get()"), std::string::npos);
    EXPECT_NE(wiring.find("setSupplierToolImportDialog(importDialog)"),
              std::string::npos);
    EXPECT_NE(wiring.find("tbp->refresh()"), std::string::npos);

    EXPECT_NE(browser.find("ImGuiItemFlags_MixedValue"), std::string::npos);
    EXPECT_NE(browser.find("m_tree.setBranchSelected"), std::string::npos);
    EXPECT_NE(browser.find("m_activeFolderId = folderId"), std::string::npos);
    EXPECT_NE(browser.find("!ImGui::IsItemToggledOpen()"), std::string::npos);
    EXPECT_NE(browser.find("matchingToolIds"), std::string::npos);
    EXPECT_NE(browser.find("sideBySideThreshold"), std::string::npos);
}
