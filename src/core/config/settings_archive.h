#pragma once

#include <string>
#include <vector>

#include "../types.h"

namespace dw {

// Categories of settings that can be exported/imported
enum class SettingsCategory {
    UIPreferences,      // Theme, scale, navigation, render
    WindowLayout,       // imgui.ini (docking, window positions/sizes)
    MachineProfiles,    // Machine definitions (spindle, travel, feeds)
    LayoutPresets,      // Panel visibility presets
    InputBindings,      // Keybindings
    CncSettings,        // CNC operation parameters, safety, WCS aliases
    DirectoryPaths,     // Workspace root, category dirs
    ImportSettings,     // Import pipeline preferences
    ToolDatabase,       // tools.vtdb file
    RecentFiles,        // Recent projects and G-code files
    ApiKeys,            // API keys (LM Studio, etc.)

    Count
};

const char* settingsCategoryName(SettingsCategory cat);
const char* settingsCategoryDescription(SettingsCategory cat);

// Metadata embedded in the archive
struct SettingsArchiveManifest {
    int version = 1;
    std::string appVersion;
    std::string exportDate;
    std::string hostname;
    std::vector<SettingsCategory> categories; // what's included
};

// Export all settings to a .dwsettings archive
bool exportSettings(const Path& archivePath);

// Read the manifest from an archive (to show what's available for import)
bool readSettingsManifest(const Path& archivePath, SettingsArchiveManifest& manifest);

// Import selected categories from a .dwsettings archive
// Returns true if at least one category was restored
bool importSettings(const Path& archivePath,
                    const std::vector<SettingsCategory>& selected);

} // namespace dw
