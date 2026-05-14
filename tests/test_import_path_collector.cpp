#include <gtest/gtest.h>

#include "core/import/import_path_collector.h"
#include "core/utils/file_utils.h"

#include <filesystem>
#include <system_error>

namespace {

class TempDir {
  public:
    TempDir() {
        m_path = std::filesystem::temp_directory_path() / "dw_test_import_path_collector";
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempDir() { std::filesystem::remove_all(m_path); }

    dw::Path path() const { return m_path; }
    dw::Path operator/(const std::string& name) const { return m_path / name; }

  private:
    dw::Path m_path;
};

void writeFile(const dw::Path& path) {
    std::filesystem::create_directories(path.parent_path());
    ASSERT_TRUE(dw::file::writeText(path, "test"));
}

} // namespace

TEST(ImportPathCollector, RecursivelyCollectsSupportedModelFiles) {
    TempDir tmp;
    writeFile(tmp / "top.stl");
    writeFile(tmp / "nested" / "part.obj");
    writeFile(tmp / "nested" / "deeper" / "shape.3mf");
    writeFile(tmp / "nested" / "ignore.txt");

    auto paths = dw::import_paths::collectSupportedModelFiles(tmp.path());

    ASSERT_EQ(paths.size(), 3u);
    EXPECT_EQ(paths[0].filename(), "top.stl");
    EXPECT_EQ(paths[1].filename(), "part.obj");
    EXPECT_EQ(paths[2].filename(), "shape.3mf");
}

TEST(ImportPathCollector, MatchesModelExtensionsCaseInsensitively) {
    TempDir tmp;
    writeFile(tmp / "upper.STL");
    writeFile(tmp / "mixed.Obj");

    auto paths = dw::import_paths::collectSupportedModelFiles(tmp.path());

    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0].filename(), "mixed.Obj");
    EXPECT_EQ(paths[1].filename(), "upper.STL");
}

TEST(ImportPathCollector, SupportsSingleFileInputs) {
    TempDir tmp;
    writeFile(tmp / "single.stl");

    auto paths = dw::import_paths::collectSupportedModelFiles(tmp / "single.stl");

    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths[0].filename(), "single.stl");
}

TEST(ImportPathCollector, ReturnsAbsolutePathsForRelativeInputs) {
    TempDir tmp;
    writeFile(tmp / "Animals" / "animals7.stl");

    std::error_code ec;
    auto oldCwd = std::filesystem::current_path(ec);
    ASSERT_FALSE(ec);
    std::filesystem::current_path(tmp.path(), ec);
    ASSERT_FALSE(ec);

    auto paths = dw::import_paths::collectSupportedModelFiles(dw::Path("Animals"));

    std::filesystem::current_path(oldCwd, ec);
    ASSERT_FALSE(ec);

    ASSERT_EQ(paths.size(), 1u);
    EXPECT_TRUE(paths[0].is_absolute());
    EXPECT_EQ(paths[0].filename(), "animals7.stl");
    EXPECT_EQ(paths[0].parent_path().filename(), "Animals");
}

TEST(ImportPathCollector, ReportsProgressWhileScanningDirectories) {
    TempDir tmp;
    writeFile(tmp / "top.stl");
    writeFile(tmp / "nested" / "part.obj");
    writeFile(tmp / "nested" / "ignore.txt");

    dw::import_paths::ScanProgress progress;
    std::vector<std::string> currentItems;

    auto paths = dw::import_paths::collectSupportedModelFiles(
        tmp.path(), [&progress, &currentItems](const dw::import_paths::ScanProgress& update) {
            progress = update;
            currentItems.push_back(update.currentPath.filename().string());
            return true;
        });

    ASSERT_EQ(paths.size(), 2u);
    EXPECT_GE(progress.directoriesVisited, 2);
    EXPECT_EQ(progress.filesVisited, 3);
    EXPECT_EQ(progress.supportedFilesFound, 2);
    EXPECT_FALSE(currentItems.empty());
}

TEST(ImportPathCollector, StopsWhenProgressCallbackCancels) {
    TempDir tmp;
    writeFile(tmp / "a.stl");
    writeFile(tmp / "b.obj");

    int callbackCount = 0;
    auto paths = dw::import_paths::collectSupportedModelFiles(
        tmp.path(), [&callbackCount](const dw::import_paths::ScanProgress&) {
            ++callbackCount;
            return false;
        });

    EXPECT_TRUE(paths.empty());
    EXPECT_EQ(callbackCount, 1);
}
