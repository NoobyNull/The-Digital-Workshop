#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string readSource(const fs::path& relativePath) {
    const auto path = fs::path(CMAKE_SOURCE_DIR) / relativePath;
    std::ifstream input(path);
    EXPECT_TRUE(input.is_open()) << path;
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void expectContains(const std::string& source, const char* token) {
    EXPECT_NE(source.find(token), std::string::npos) << token;
}

} // namespace

TEST(GuidedAccessibilityArchitecture, KeyboardNavigationReachesPrimaryWorkflowActions) {
    const auto application = readSource("src/app/application.cpp");
    const auto projectPlan = readSource("src/ui/panels/project_plan_view.cpp");
    const auto libraryPicker =
        readSource("src/modules/design_library/ui/library_picker_view.cpp");
    const auto libraryItems = readSource("src/ui/panels/library_panel_items.cpp");
    const auto guidedSurfaces = projectPlan + libraryPicker + libraryItems;

    expectContains(application, "ImGuiConfigFlags_NavEnableKeyboard");
    expectContains(projectPlan, "ImGui::Button");
    expectContains(projectPlan, "ImGui::SmallButton");
    expectContains(libraryPicker, "ImGui::InputText");
    expectContains(libraryPicker, "ImGui::SetKeyboardFocusHere");
    expectContains(libraryItems, "ImGui::Selectable");
    EXPECT_EQ(guidedSurfaces.find("ImGuiWindowFlags_NoNav"), std::string::npos);
}

TEST(GuidedAccessibilityArchitecture, ProjectAndLibraryStateRemainExplicitWithoutColor) {
    const auto projectPlan = readSource("src/ui/panels/project_plan_view.cpp");
    const auto contextBar =
        readSource("src/modules/workshop/ui/project_context_bar.cpp");
    const auto libraryPicker =
        readSource("src/modules/design_library/ui/library_picker_view.cpp");
    const auto libraryItems = readSource("src/ui/panels/library_panel_items.cpp");

    for (const auto* token : {"stageStateLabel(stage.state)",
                              "itemStateLabel(node.item.state)",
                              "nodeRoleLabel(node.role)",
                              "NEXT STEP:",
                              "Information:"}) {
        expectContains(projectPlan, token);
    }
    for (const auto* token : {"presentation.projectLabel",
                              "presentation.machineLabel",
                              "Unsaved",
                              "Back to Project"}) {
        expectContains(contextBar, token);
    }
    for (const auto* token : {"presentation.selectionText",
                              "presentation.membershipText",
                              "presentation.statusText",
                              "presentation.errorText"}) {
        expectContains(libraryPicker, token);
    }
    expectContains(libraryItems, "IN PROJECT");
}

TEST(GuidedAccessibilityArchitecture, ResponsiveChoicesUseAvailableSpaceAndStyleMetrics) {
    const auto libraryPicker =
        readSource("src/modules/design_library/ui/library_picker_view.cpp");
    const auto contextBar =
        readSource("src/modules/workshop/ui/project_context_bar.cpp");

    expectContains(libraryPicker, "chooseLibraryPickerActionLayout");
    expectContains(libraryPicker, "ImGui::GetContentRegionAvail().x");
    expectContains(libraryPicker, "ImVec2(-1.0F, 0.0F)");
    expectContains(contextBar, "ImGuiTableFlags_SizingStretchProp");
    expectContains(contextBar, "ImGuiTableColumnFlags_WidthFixed");
    expectContains(contextBar, "oneRowColumns.project");
    expectContains(contextBar, "oneRowColumns.action");
    expectContains(contextBar, "viewport->WorkSize.x");
}

TEST(GuidedAccessibilityArchitecture, NarrowLibraryCopyWrapsInOwnedTextBlocks) {
    const auto libraryPicker =
        readSource("src/modules/design_library/ui/library_picker_view.cpp");

    expectContains(libraryPicker, "renderWrappedText(presentation.heading)");
    expectContains(libraryPicker, "renderWrappedText(presentation.guidance");
    expectContains(libraryPicker, "renderWrappedText(presentation.selectionText)");
    expectContains(libraryPicker, "renderWrappedText(presentation.membershipText");
    expectContains(libraryPicker, "renderWrappedText(presentation.statusText");
    expectContains(libraryPicker, "renderWrappedText(presentation.errorText");
}

TEST(GuidedAccessibilityArchitecture, ProgrammaticLibrarySelectionScrollsIntoView) {
    const auto picker = readSource("src/ui/panels/library_panel_picker.cpp");
    const auto items = readSource("src/ui/panels/library_panel_items.cpp");

    expectContains(picker, "m_pendingSelectionReveal = item");
    expectContains(items, "revealPendingSelection");
    expectContains(items, "ImGui::SetScrollHereY(1.0F)");
    const std::string lists = readSource("src/ui/panels/library_panel_lists.cpp");
    expectContains(lists, "renderCardListBottomClearance");
    expectContains(lists, "ImGui::GetTextLineHeightWithSpacing()");
}

TEST(GuidedAccessibilityArchitecture, GuidedLibraryUsesLiveExperienceAndHidesProperties) {
    const auto picker = readSource("src/app/application_library_picker.cpp");
    const auto wiring = readSource("src/app/application_wiring_library.cpp");

    expectContains(picker, "libraryExperienceMode() const");
    expectContains(picker, "NavigateWorkshopIntent{\n            experience");
    expectContains(picker, "showProperties() = !guided");
    expectContains(picker, "ReturnFromLibraryIntent{\n                    libraryExperienceMode()");
    expectContains(wiring, "PreviewLibraryItemIntent{\n                                            libraryExperienceMode()");
    EXPECT_EQ(wiring.find("workshop::ExperienceMode::Advanced"), std::string::npos);
}

