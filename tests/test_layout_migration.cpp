#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/config/layout_migration.h"

namespace dw {
namespace {

LayoutPreset legacyWorkshop() {
    LayoutPreset preset;
    preset.name = "Workshop";
    preset.builtIn = true;
    preset.visibility = {
        {"viewport", true},
        {"properties", true},
        {"project", true},
        {"start_page", false},
    };
    return preset;
}

LayoutPreset legacySender() {
    LayoutPreset preset;
    preset.name = "CNC Sender";
    preset.builtIn = true;
    preset.autoTriggerPanelKey = "cnc_status";
    preset.visibility = {
        {"viewport", true},
        {"gcode", true},
        {"cnc_status", true},
    };
    return preset;
}

LayoutPreset customPreset(std::string name, bool viewportVisible) {
    LayoutPreset preset;
    preset.name = std::move(name);
    preset.visibility = {
        {"viewport", viewportVisible},
        {"cost_estimator", true},
        {"private_plugin_panel", true},
    };
    preset.autoTriggerPanelKey = "private_plugin_panel";
    return preset;
}

std::vector<std::string> serialized(const std::vector<LayoutPreset>& presets) {
    std::vector<std::string> values;
    values.reserve(presets.size());
    for (const auto& preset : presets)
        values.push_back(preset.toJsonString());
    return values;
}

TEST(LayoutMigration, LegacyBuiltInsUpgradeOnceAndStayOnAdvanced) {
    const auto result = migrateGuidedLayouts({0, 0, {legacyWorkshop(), legacySender()}});

    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.version, CURRENT_LAYOUT_MIGRATION_VERSION);
    ASSERT_EQ(result.presets.size(), 3U);
    EXPECT_TRUE(isGuidedLayout(result.presets[0]));
    EXPECT_TRUE(isAdvancedLayout(result.presets[1]));
    EXPECT_TRUE(isCncLayout(result.presets[2]));
    EXPECT_EQ(result.activePresetIndex, 1);
    EXPECT_TRUE(result.presets[0].visibility.at("start_page"));
    EXPECT_FALSE(result.presets[0].visibility.at("properties"));
}

TEST(LayoutMigration, CustomPresetsSurviveByteEquivalentAndKeepSelection) {
    const auto first = customPreset("My shop", false);
    const auto active = customPreset("Router close-up", true);
    const std::string firstBefore = first.toJsonString();
    const std::string activeBefore = active.toJsonString();

    const auto result =
        migrateGuidedLayouts({0, 3, {legacyWorkshop(), first, legacySender(), active}});

    ASSERT_EQ(result.presets.size(), 5U);
    EXPECT_EQ(result.activePresetIndex, 4);
    EXPECT_EQ(result.presets[3].toJsonString(), firstBefore);
    EXPECT_EQ(result.presets[4].toJsonString(), activeBefore);
    EXPECT_FALSE(result.presets[3].builtIn);
    EXPECT_TRUE(result.presets[4].visibility.at("private_plugin_panel"));
}

TEST(LayoutMigration, CustomPresetNamedLikeLegacyBuiltInIsStillCustom) {
    auto custom = customPreset("Workshop", true);
    ASSERT_FALSE(custom.builtIn);
    const auto before = custom.toJsonString();

    const auto result = migrateGuidedLayouts({0, 0, {custom}});

    ASSERT_EQ(result.presets.size(), 4U);
    EXPECT_EQ(result.activePresetIndex, 3);
    EXPECT_EQ(result.presets[3].toJsonString(), before);
}

TEST(LayoutMigration, RerunningCurrentVersionIsAnExactNoOp) {
    const auto migrated = migrateGuidedLayouts(
        {0, 2, {legacyWorkshop(), legacySender(), customPreset("Custom", true)}});
    const auto before = serialized(migrated.presets);

    const auto rerun =
        migrateGuidedLayouts({migrated.version, migrated.activePresetIndex, migrated.presets});

    EXPECT_FALSE(rerun.changed);
    EXPECT_EQ(rerun.version, migrated.version);
    EXPECT_EQ(rerun.activePresetIndex, migrated.activePresetIndex);
    EXPECT_EQ(serialized(rerun.presets), before);
}

TEST(LayoutMigration, FutureVersionIsNeverDowngradedOrRewritten) {
    const auto custom = customPreset("Future custom", true);
    const auto result = migrateGuidedLayouts({CURRENT_LAYOUT_MIGRATION_VERSION + 4, 0, {custom}});

    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.version, CURRENT_LAYOUT_MIGRATION_VERSION + 4);
    ASSERT_EQ(result.presets.size(), 1U);
    EXPECT_EQ(result.presets.front().toJsonString(), custom.toJsonString());
}

TEST(LayoutMigration, BuiltInIdsRoundTripWhileCustomJsonShapeStaysStable) {
    const auto guided = LayoutPreset::guidedDefault();
    const auto parsedGuided = LayoutPreset::fromJsonString(guided.toJsonString());
    EXPECT_EQ(parsedGuided.id, GUIDED_LAYOUT_ID);

    const auto custom = customPreset("No built-in id", true);
    const auto json = custom.toJsonString();
    EXPECT_EQ(json.find("\"id\""), std::string::npos);
    EXPECT_EQ(LayoutPreset::fromJsonString(json).toJsonString(), json);
}

TEST(LayoutMigrationArchitecture, ConfigDispatcherNoLongerOwnsLayoutPolicy) {
    std::ifstream source(std::string(CMAKE_SOURCE_DIR) + "/src/core/config/config.cpp");
    ASSERT_TRUE(source.is_open());
    std::ostringstream content;
    content << source.rdbuf();

    EXPECT_EQ(content.str().find("migrateGuidedLayouts"), std::string::npos);
    EXPECT_EQ(content.str().find("Config::resolveLoadedLayoutPresets"), std::string::npos);

    std::size_t lines = 0;
    std::string line;
    std::istringstream count(content.str());
    while (std::getline(count, line))
        ++lines;
    EXPECT_LE(lines, 750U);
}

TEST(LayoutMigrationArchitecture, ExperienceSelectionUsesControllerState) {
    std::ifstream wiring(std::string(CMAKE_SOURCE_DIR) +
                         "/src/app/application_project_session.cpp");
    ASSERT_TRUE(wiring.is_open());
    std::ostringstream content;
    content << wiring.rdbuf();

    EXPECT_NE(content.str().find("setExperienceModeAccessors"), std::string::npos);
    EXPECT_NE(content.str().find("m_projectWorkshopController->guidedEnabled()"),
              std::string::npos);
    EXPECT_NE(content.str().find("m_projectWorkshopController->setGuidedEnabled(guided)"),
              std::string::npos);
}

} // namespace
} // namespace dw
