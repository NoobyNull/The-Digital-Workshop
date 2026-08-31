// Digital Workshop - Import thumbnail sidecar tests

#include <gtest/gtest.h>

#include "core/import/thumbnail_sidecar.h"
#include "core/utils/file_utils.h"

#include <filesystem>

namespace {

class ThumbnailSidecarTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_tmpDir = std::filesystem::temp_directory_path() / "dw_test_thumbnail_sidecar";
        std::filesystem::remove_all(m_tmpDir);
        std::filesystem::create_directories(m_tmpDir);
    }

    void TearDown() override { std::filesystem::remove_all(m_tmpDir); }

    dw::Path touch(const std::string& name) {
        const auto path = m_tmpDir / name;
        EXPECT_TRUE(dw::file::writeText(path, "x"));
        return path;
    }

    std::filesystem::path m_tmpDir;
};

} // namespace

TEST_F(ThumbnailSidecarTest, FindsExactStemImageBesideModel) {
    const auto modelPath = touch("Router Sign.stl");
    const auto imagePath = touch("Router Sign.png");

    auto found = dw::findSidecarThumbnailForImport(modelPath);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(std::filesystem::equivalent(*found, imagePath), true);
}

TEST_F(ThumbnailSidecarTest, MatchesStemAndExtensionCaseInsensitively) {
    const auto modelPath = touch("eagle scout.STL");
    const auto imagePath = touch("EAGLE SCOUT.JPG");

    auto found = dw::findSidecarThumbnailForImport(modelPath);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(std::filesystem::equivalent(*found, imagePath), true);
}

TEST_F(ThumbnailSidecarTest, FindsCommonThumbnailSuffixes) {
    const auto modelPath = touch("gearbox.3mf");
    const auto imagePath = touch("gearbox-preview.jpeg");

    auto found = dw::findSidecarThumbnailForImport(modelPath);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(std::filesystem::equivalent(*found, imagePath), true);
}

TEST_F(ThumbnailSidecarTest, IgnoresUnrelatedImagesInSameDirectory) {
    const auto modelPath = touch("bracket.obj");
    touch("random-render.png");

    auto found = dw::findSidecarThumbnailForImport(modelPath);

    EXPECT_FALSE(found.has_value());
}

TEST_F(ThumbnailSidecarTest, PrefersExactStemOverSuffixImage) {
    const auto modelPath = touch("handle.stl");
    const auto exactPath = touch("handle.png");
    touch("handle-preview.png");

    auto found = dw::findSidecarThumbnailForImport(modelPath);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(std::filesystem::equivalent(*found, exactPath), true);
}
