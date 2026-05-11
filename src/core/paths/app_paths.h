#pragma once

#include "../types.h"

#include <string_view>

namespace dw {
namespace paths {

// Platform-specific application directories
// Linux:   ~/.config/digitalworkshop/, ~/.local/share/digitalworkshop/
// Windows: %APPDATA%/DigitalWorkshop/, %LOCALAPPDATA%/DigitalWorkshop/
// macOS:   ~/Library/Application Support/DigitalWorkshop/

// Configuration directory (settings, preferences)
Path getConfigDir();

// Data directory (database, cache, thumbnails)
Path getDataDir();

// Default user projects directory
Path getDefaultProjectsDir();

// Cache directory (temporary files, thumbnail cache)
Path getCacheDir();

// Thumbnail cache directory
Path getThumbnailDir();

// Database file path
Path getDatabasePath();

// Tool database file path (.vtdb format)
Path getToolDatabasePath();

// Macro database file path (SQLite)
Path getMacroDatabasePath();

// Log file path
Path getLogPath();

// Content-addressable blob store directory
Path getBlobStoreDir();

// Temporary file directory for atomic writes (co-located with blob store)
Path getTempStoreDir();

// Materials directory (app-managed .dwmat files)
Path getMaterialsDir();

// User root directory (~/DigitalWorkshop)
Path getUserRoot();

// Default category directories under user root
Path getDefaultModelsDir();
Path getDefaultGCodeDir();
Path getDefaultMaterialsDir();
Path getDefaultSupportDir();

// Bundled materials directory (shipped .dwmat files next to the executable)
// Returns <exe_dir>/resources/materials/
Path getBundledMaterialsDir();

// Bundled icons directory (shipped icons next to the executable)
// Returns <exe_dir>/resources/icons/
Path getBundledIconsDir();

// Resolve bundled resources for both build-tree and installed Linux layouts.
Path findBundledResourceDirForExe(const Path& exeDir, std::string_view leafDir);

// Ensure all application directories exist
bool ensureDirectoriesExist();

// Get application name (used in paths)
const char* getAppName();

} // namespace paths
} // namespace dw
