// Digital Workshop - App Paths Tests

#include <gtest/gtest.h>

#include "core/paths/app_paths.h"

#include <cstring>
#include <filesystem>

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
