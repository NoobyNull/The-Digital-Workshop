#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core/config/layout_preset.h"
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

TEST(WindowCatalog, DockableLayoutKeysExistInBuiltInPresets) {
    const auto workshop = LayoutPreset::modelDefault();
    const auto sender = LayoutPreset::cncDefault();

    for (const auto& entry : windowCatalogEntries()) {
        if (entry.type != WindowType::DockablePanel || !entry.layoutPersistent)
            continue;

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
