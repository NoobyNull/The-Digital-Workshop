#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core/config/layout_preset.h"
#include "core/config/workspace_stream_policy.h"
#include "core/config/window_catalog.h"

namespace dw {
namespace {

TEST(WindowCatalog, ToolLibraryUsesCanonicalKeyWithLegacyLayoutKey) {
    const auto* entry = findWindowCatalogEntry("tool_library");
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->key, "tool_library");
    EXPECT_EQ(entry->layoutKey, "tool_browser");
    EXPECT_EQ(canonicalWindowKey("tool_browser"), "tool_library");
    EXPECT_TRUE(entry->hasLegacyKey("tool_browser"));
}

TEST(WindowCatalog, MachineProfilesIsGlobalServiceWindow) {
    const auto* entry = findWindowCatalogEntry("machine_profiles");
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->title, "Machine Profiles");
    EXPECT_EQ(entry->role, WindowRole::Global);
    EXPECT_EQ(entry->type, WindowType::FloatingService);
    EXPECT_FALSE(entry->layoutPersistent);
}

TEST(WindowCatalog, HomePreservesTheStartPageLayoutIdentity) {
    const auto* entry = findWindowCatalogEntry("start_page");
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->layoutKey, "start_page");
    EXPECT_EQ(entry->title, "Home###Start Page");
    EXPECT_EQ(entry->menuLabel, "Home");
    EXPECT_TRUE(entry->hasLegacyKey("home"));
    EXPECT_EQ(canonicalWindowKey("home"), "start_page");
}

TEST(WindowCatalog, ProjectCostingPreservesItsLegacyLayoutAlias) {
    const auto* entry = findWindowCatalogEntry("cost_estimator");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->key, "project_costing");
    EXPECT_EQ(entry->layoutKey, "project_costing");
}

TEST(WindowCatalog, LegacyVisibilityKeysResolveWithoutRewritingCustomPreset) {
    LayoutPreset custom;
    custom.name = "Legacy aliases";
    custom.visibility = {{"cost_estimator", true}, {"home", false}};
    const auto before = custom.toJsonString();

    EXPECT_EQ(layoutPresetVisibility(custom, "project_costing"), true);
    EXPECT_EQ(layoutPresetVisibility(custom, "start_page"), false);
    EXPECT_EQ(custom.toJsonString(), before);
}

TEST(WindowCatalog, DesignLibraryIsATransientWorkflowSurface) {
    const auto* entry = findWindowCatalogEntry("library");
    ASSERT_NE(entry, nullptr);

    EXPECT_FALSE(entry->workshopDefaultVisible);
    EXPECT_FALSE(entry->layoutPersistent);
    EXPECT_FALSE(LayoutPreset::modelDefault().visibility.at("library"));
}

TEST(WindowCatalog, DirectCarveIsSharedAcrossWorkshopAndSender) {
    const auto* entry = findWindowCatalogEntry("direct_carve");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->role, WindowRole::Shared);

    std::ifstream manager(std::string(CMAKE_SOURCE_DIR) + "/src/managers/ui_manager.cpp");
    ASSERT_TRUE(manager.is_open());
    std::ostringstream source;
    source << manager.rdbuf();
    const auto directCarve = source.str().find("{\"direct_carve\"");
    ASSERT_NE(directCarve, std::string::npos);
    EXPECT_NE(source.str().find("WindowRole::Shared", directCarve), std::string::npos);
}

TEST(WorkspaceStreamPolicy, OnlyGuidedDirectCarvePreservesWorkshopShell) {
    EXPECT_EQ(cncStreamShellForStart(
                  CncStreamOrigin::DirectCarve, true, true),
              CncStreamShell::GuidedWorkshop);
    EXPECT_EQ(cncStreamShellForStart(
                  CncStreamOrigin::DirectCarve, false, true),
              CncStreamShell::Sender);
    EXPECT_EQ(cncStreamShellForStart(
                  CncStreamOrigin::DirectCarve, true, false),
              CncStreamShell::Sender);
    EXPECT_EQ(cncStreamShellForStart(
                  CncStreamOrigin::ExternalGCode, true, true),
              CncStreamShell::Sender);
}

TEST(WindowCatalog, DockableLayoutKeysExistInBuiltInPresets) {
    const auto guided = LayoutPreset::guidedDefault();
    const auto workshop = LayoutPreset::modelDefault();
    const auto sender = LayoutPreset::cncDefault();

    for (const auto& entry : windowCatalogEntries()) {
        if (entry.type != WindowType::DockablePanel || !entry.layoutPersistent)
            continue;

        EXPECT_NE(guided.visibility.find(entry.layoutKey), guided.visibility.end()) << entry.key;
        EXPECT_NE(workshop.visibility.find(entry.layoutKey), workshop.visibility.end())
            << entry.key;
        EXPECT_NE(sender.visibility.find(entry.layoutKey), sender.visibility.end())
            << entry.key;
    }
}

TEST(WindowCatalog, SerializesToJsonForCrossPlatformSettings) {
    const auto jsonText = windowCatalogJson();
    const auto json = nlohmann::json::parse(jsonText);

    ASSERT_TRUE(json.is_array());
    ASSERT_FALSE(json.empty());

    const auto* machine = findWindowCatalogEntry("machine_profiles");
    ASSERT_NE(machine, nullptr);

    auto it = std::find_if(json.begin(), json.end(), [](const auto& item) {
        return item.value("key", "") == "machine_profiles";
    });
    ASSERT_NE(it, json.end());
    EXPECT_EQ(it->value("role", ""), "global");
    EXPECT_EQ(it->value("type", ""), "floating_service");
}

TEST(WindowCatalog, ImportSummaryHasSingleRenderOwner) {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) + "/src/managers/ui_manager.cpp");
    ASSERT_TRUE(file.is_open());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    const bool renderedThroughDialogList =
        source.find("m_importSummaryDialog.get()") != std::string::npos;
    const bool renderedExplicitly =
        source.find("m_importSummaryDialog->render();") != std::string::npos;

    EXPECT_FALSE(renderedThroughDialogList && renderedExplicitly);
}

} // namespace
} // namespace dw
