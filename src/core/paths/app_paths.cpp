#include "app_paths.h"

#include <cstdlib>
#include <system_error>
#include <string_view>

#include "../config/config.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shlobj.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <pwd.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace dw {
namespace paths {

namespace {

const char* APP_NAME = "digitalworkshop";
const char* APP_DISPLAY_NAME = "DigitalWorkshop";

#ifdef _WIN32
Path getWindowsKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &path))) {
        Path result(path);
        CoTaskMemFree(path);
        return result;
    }
    return Path();
}
#endif

Path getHomeDir() {
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return Path(userProfile);
    }
    return getWindowsKnownFolder(FOLDERID_Profile);
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return Path(home);
    }
    // Fallback: look up home dir from passwd entry
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return Path(pw->pw_dir);
    }
    log::error("Paths", "Cannot determine home directory: $HOME unset and getpwuid failed");
    return fs::temp_directory_path();
#endif
}

// Directory containing the running executable
Path getExeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return Path(std::string(buf, len)).parent_path();
    }
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        return fs::canonical(Path(buf)).parent_path();
    }
#else
    std::error_code ec;
    Path exePath = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exePath.parent_path();
    }
#endif
    return Path(".");
}

} // namespace

const char* getAppName() {
    return APP_NAME;
}

Path getConfigDir() {
#ifdef _WIN32
    Path appData = getWindowsKnownFolder(FOLDERID_RoamingAppData);
    if (appData.empty()) {
        appData = getHomeDir() / "AppData" / "Roaming";
    }
    return appData / APP_DISPLAY_NAME;
#elif defined(__APPLE__)
    return getHomeDir() / "Library" / "Application Support" / APP_DISPLAY_NAME;
#else
    // Linux - XDG Base Directory
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig && xdgConfig[0] != '\0') {
        return Path(xdgConfig) / APP_NAME;
    }
    return getHomeDir() / ".config" / APP_NAME;
#endif
}

Path getDataDir() {
#ifdef _WIN32
    Path localAppData = getWindowsKnownFolder(FOLDERID_LocalAppData);
    if (localAppData.empty()) {
        localAppData = getHomeDir() / "AppData" / "Local";
    }
    return localAppData / APP_DISPLAY_NAME;
#elif defined(__APPLE__)
    return getHomeDir() / "Library" / "Application Support" / APP_DISPLAY_NAME;
#else
    // Linux - XDG Base Directory
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        return Path(xdgData) / APP_NAME;
    }
    return getHomeDir() / ".local" / "share" / APP_NAME;
#endif
}

Path getDefaultProjectsDir() {
    return getUserRoot() / "Projects";
}

Path getCacheDir() {
#ifdef _WIN32
    return getDataDir() / "cache";
#elif defined(__APPLE__)
    return getHomeDir() / "Library" / "Caches" / APP_DISPLAY_NAME;
#else
    const char* xdgCache = std::getenv("XDG_CACHE_HOME");
    if (xdgCache && xdgCache[0] != '\0') {
        return Path(xdgCache) / APP_NAME;
    }
    return getHomeDir() / ".cache" / APP_NAME;
#endif
}

Path getThumbnailDir() {
    return getCacheDir() / "thumbnails";
}

Path getDatabasePath() {
    return getDataDir() / "library.db";
}

Path getToolDatabasePath() {
    return getDataDir() / "tools.vtdb";
}

Path getMacroDatabasePath() {
    return getDataDir() / "macros.db";
}

Path getLogPath() {
    return getDataDir() / "digital_workshop.log";
}

Path getBlobStoreDir() {
    return getDataDir() / "blobs";
}

Path getTempStoreDir() {
    return getBlobStoreDir() / ".tmp";
}

Path getMaterialsDir() {
    return getDataDir() / "materials";
}

Path findBundledResourceDirForExe(const Path& exeDir, std::string_view leafDir) {
    Path exeRelative = exeDir / "resources" / std::string(leafDir);
    if (file::isDirectory(exeRelative)) {
        return exeRelative;
    }

#ifdef __linux__
    Path prefix = exeDir.parent_path();
    Path prefixShare = prefix / "share" / APP_NAME / "resources" / std::string(leafDir);
    if (file::isDirectory(prefixShare)) {
        return prefixShare;
    }
#endif

    return exeRelative;
}

Path getBundledMaterialsDir() {
    return findBundledResourceDirForExe(getExeDir(), "materials");
}

Path getBundledIconsDir() {
    return findBundledResourceDirForExe(getExeDir(), "icons");
}

Path getUserRoot() {
    return getHomeDir() / APP_DISPLAY_NAME;
}

Path getDefaultModelsDir() {
    return getUserRoot() / "Models";
}

Path getDefaultGCodeDir() {
    return getUserRoot() / "GCode";
}

Path getDefaultMaterialsDir() {
    return getUserRoot() / "Materials";
}

Path getDefaultSupportDir() {
    return getUserRoot() / "Support";
}

bool ensureDirectoriesExist() {
    bool success = true;

    auto ensureDir = [&success](const Path& path, const char* name) {
        if (!file::createDirectories(path)) {
            log::errorf("Paths", "Failed to create %s directory: %s", name, path.string().c_str());
            success = false;
        } else {
            log::debugf("Paths", "Ensured %s directory: %s", name, path.string().c_str());
        }
    };

    // Internal directories (XDG locations)
    ensureDir(getConfigDir(), "config");
    ensureDir(getDataDir(), "data");
    ensureDir(getCacheDir(), "cache");
    ensureDir(getThumbnailDir(), "thumbnail");
    ensureDir(getBlobStoreDir(), "blob store");
    ensureDir(getTempStoreDir(), "temp store");

    // User-visible directories (from Config, defaults to ~/DigitalWorkshop/*)
    auto& cfg = Config::instance();
    ensureDir(cfg.getModelsDir(), "models");
    ensureDir(cfg.getProjectsDir(), "projects");
    ensureDir(cfg.getMaterialsDir(), "materials");
    ensureDir(cfg.getGCodeDir(), "gcode");
    ensureDir(cfg.getSupportDir(), "support");
    ensureDir(cfg.getSupportDir() / "blobs", "support blobs");
    ensureDir(cfg.getSupportDir() / "blobs" / ".tmp", "support blobs tmp");

    return success;
}

std::vector<Path> getFactoryResetTargets() {
    return {
        getConfigDir(),
        getDataDir(),
        getCacheDir(),
        getUserRoot(),
    };
}

FactoryResetResult resetUserStateToDefaults() {
    FactoryResetResult result;

    for (const auto& path : getFactoryResetTargets()) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            if (ec) {
                result.success = false;
                result.error = "Failed to inspect " + path.string() + ": " + ec.message();
                return result;
            }
            continue;
        }

        fs::remove_all(path, ec);
        if (ec) {
            result.success = false;
            result.error = "Failed to remove " + path.string() + ": " + ec.message();
            return result;
        }
        result.removedPaths.push_back(path);
        log::infof("Paths", "Factory reset removed %s", path.string().c_str());
    }

    return result;
}

} // namespace paths
} // namespace dw
