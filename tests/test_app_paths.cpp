// Digital Workshop - App Paths Tests

#include <gtest/gtest.h>

#include "test_env_util.h"

#include "core/paths/app_paths.h"

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <optional>

namespace {

class ScopedEnvVar {
  public:
    ScopedEnvVar(const char* name, const std::string& value) : m_name(name) {
        const char* existing = std::getenv(name);
        if (existing != nullptr) {
            m_oldValue = existing;
        }
        dw::test::setEnv(name, value.c_str());
    }

    ~ScopedEnvVar() {
        if (!m_oldValue) {
            dw::test::unsetEnv(m_name.c_str());
        } else {
            dw::test::setEnv(m_name.c_str(), m_oldValue->c_str());
        }
    }

  private:
    std::string m_name;
    std::optional<std::string> m_oldValue;
};

} // namespace

TEST(AppPaths, GetConfigDir_NonEmpty) {
    auto dir = dw::paths::getConfigDir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(dir.is_absolute());
}

TEST(AppPaths, GetDataDir_NonEmpty) {
    auto dir = dw::paths::getDataDir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(dir.is_absolute());
}

TEST(AppPaths, GetDefaultProjectsDir_NonEmpty) {
    auto dir = dw::paths::getDefaultProjectsDir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(dir.is_absolute());
}

TEST(AppPaths, GetCacheDir_NonEmpty) {
    auto dir = dw::paths::getCacheDir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(dir.is_absolute());
}

TEST(AppPaths, GetThumbnailDir_NonEmpty) {
    auto dir = dw::paths::getThumbnailDir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(dir.is_absolute());
}

TEST(AppPaths, GetDatabasePath_HasFilename) {
    auto path = dw::paths::getDatabasePath();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.is_absolute());
    // Should end with a filename (has a stem)
    EXPECT_FALSE(path.filename().empty());
}

TEST(AppPaths, GetLogPath_HasFilename) {
    auto path = dw::paths::getLogPath();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.is_absolute());
}

TEST(AppPaths, GetAppName_NonEmpty) {
    const char* name = dw::paths::getAppName();
    EXPECT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0u);
}

TEST(AppPaths, EnsureDirectoriesExist) {
    EXPECT_TRUE(dw::paths::ensureDirectoriesExist());

    // Verify key dirs actually exist
    EXPECT_TRUE(std::filesystem::is_directory(dw::paths::getConfigDir()));
    EXPECT_TRUE(std::filesystem::is_directory(dw::paths::getDataDir()));
}

TEST(AppPaths, FactoryResetRemovesOnlyDigitalWorkshopOwnedUserState) {
    auto tmp = std::filesystem::temp_directory_path() / "dw_factory_reset_targets";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    ScopedEnvVar home("HOME", (tmp / "home").string());
    ScopedEnvVar xdgConfig("XDG_CONFIG_HOME", (tmp / "xdg_config").string());
    ScopedEnvVar xdgData("XDG_DATA_HOME", (tmp / "xdg_data").string());
    ScopedEnvVar xdgCache("XDG_CACHE_HOME", (tmp / "xdg_cache").string());

    const auto configDir = dw::paths::getConfigDir();
    const auto dataDir = dw::paths::getDataDir();
    const auto cacheDir = dw::paths::getCacheDir();
    const auto userRoot = dw::paths::getUserRoot();
    const auto sibling = tmp / "home" / "DoNotTouch";

    std::filesystem::create_directories(configDir);
    std::filesystem::create_directories(dataDir);
    std::filesystem::create_directories(cacheDir);
    std::filesystem::create_directories(userRoot);
    std::filesystem::create_directories(sibling);

    const auto result = dw::paths::resetUserStateToDefaults();

    EXPECT_TRUE(result.success) << result.error;
    EXPECT_FALSE(std::filesystem::exists(configDir));
    EXPECT_FALSE(std::filesystem::exists(dataDir));
    EXPECT_FALSE(std::filesystem::exists(cacheDir));
    EXPECT_FALSE(std::filesystem::exists(userRoot));
    EXPECT_TRUE(std::filesystem::exists(sibling));

    std::filesystem::remove_all(tmp);
}

#ifdef __linux__
TEST(AppPaths, BundledResourceDirFallsBackToPrefixShareLayout) {
    auto tmp = std::filesystem::temp_directory_path() / "dw_app_paths_prefix_share";
    std::filesystem::remove_all(tmp);
    auto exeDir = tmp / "prefix" / "bin";
    auto shareIcons = tmp / "prefix" / "share" / "digitalworkshop" / "resources" / "icons";
    std::filesystem::create_directories(exeDir);
    std::filesystem::create_directories(shareIcons);

    auto resolved = dw::paths::findBundledResourceDirForExe(exeDir, "icons");
    EXPECT_EQ(resolved, shareIcons);

    std::filesystem::remove_all(tmp);
}

TEST(AppPaths, BundledResourceDirPrefersExecutableRelativeLayout) {
    auto tmp = std::filesystem::temp_directory_path() / "dw_app_paths_exe_relative";
    std::filesystem::remove_all(tmp);
    auto exeDir = tmp / "prefix" / "bin";
    auto exeIcons = exeDir / "resources" / "icons";
    auto shareIcons = tmp / "prefix" / "share" / "digitalworkshop" / "resources" / "icons";
    std::filesystem::create_directories(exeIcons);
    std::filesystem::create_directories(shareIcons);

    auto resolved = dw::paths::findBundledResourceDirForExe(exeDir, "icons");
    EXPECT_EQ(resolved, exeIcons);

    std::filesystem::remove_all(tmp);
}
#endif
