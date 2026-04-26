// Digital Workshop - File Utils Tests

#include <gtest/gtest.h>

#include "core/utils/file_utils.h"

#include <filesystem>

namespace {

// RAII temp directory for test isolation
class TempDir {
  public:
    TempDir() {
        m_path = std::filesystem::temp_directory_path() / "dw_test_file_utils";
        std::filesystem::create_directories(m_path);
    }
    ~TempDir() { std::filesystem::remove_all(m_path); }

    dw::Path path() const { return m_path; }
    dw::Path operator/(const std::string& name) const { return m_path / name; }

  private:
    dw::Path m_path;
};

} // namespace

// --- getExtension / getStem ---

TEST(FileUtils, GetExtension_Basic) {
    EXPECT_EQ(dw::file::getExtension("model.stl"), "stl");
}

TEST(FileUtils, GetExtension_Uppercase) {
    EXPECT_EQ(dw::file::getExtension("model.STL"), "stl");
}

TEST(FileUtils, GetExtension_NoExtension) {
    EXPECT_EQ(dw::file::getExtension("Makefile"), "");
}

TEST(FileUtils, GetExtension_MultiDot) {
    EXPECT_EQ(dw::file::getExtension("archive.tar.gz"), "gz");
}

TEST(FileUtils, GetStem_Basic) {
    EXPECT_EQ(dw::file::getStem("/path/to/model.stl"), "model");
}

TEST(FileUtils, GetStem_NoExtension) {
    EXPECT_EQ(dw::file::getStem("/path/to/Makefile"), "Makefile");
}

TEST(FileUtils, GetParent_Basic) {
    EXPECT_EQ(dw::file::getParent("/path/to/file.txt"), dw::Path("/path/to"));
}

// --- read/write text ---

TEST(FileUtils, WriteAndReadText) {
    TempDir tmp;
    auto path = tmp / "test.txt";

    ASSERT_TRUE(dw::file::writeText(path, "hello world"));

    auto result = dw::file::readText(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "hello world");
}

TEST(FileUtils, TouchCreatesMissingFileAndPreservesExistingContent) {
    auto tmp = std::filesystem::temp_directory_path() / "dw_file_utils_touch";
    std::filesystem::remove_all(tmp);
    auto path = tmp / "nested" / "log.txt";

    ASSERT_TRUE(dw::file::touch(path));
    EXPECT_TRUE(dw::file::exists(path));

    ASSERT_TRUE(dw::file::writeText(path, "existing log\n"));
    ASSERT_TRUE(dw::file::touch(path));
    auto content = dw::file::readText(path);
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "existing log\n");

    std::filesystem::remove_all(tmp);
}

TEST(FileUtils, ReadText_NonExistent) {
    auto result = dw::file::readText("/nonexistent/path/file.txt");
    EXPECT_FALSE(result.has_value());
}

// --- read/write binary ---

TEST(FileUtils, WriteAndReadBinary) {
    TempDir tmp;
    auto path = tmp / "test.bin";

    dw::ByteBuffer data = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(dw::file::writeBinary(path, data));

    auto result = dw::file::readBinary(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), data);
}

TEST(FileUtils, ReadBinary_NonExistent) {
    auto result = dw::file::readBinary("/nonexistent/path/file.bin");
    EXPECT_FALSE(result.has_value());
}

// --- exists / isFile / isDirectory ---

TEST(FileUtils, Exists_CreatedFile) {
    TempDir tmp;
    auto path = tmp / "exists.txt";
    ASSERT_TRUE(dw::file::writeText(path, "x"));

    EXPECT_TRUE(dw::file::exists(path));
    EXPECT_TRUE(dw::file::isFile(path));
    EXPECT_FALSE(dw::file::isDirectory(path));
}

TEST(FileUtils, Exists_Directory) {
    TempDir tmp;
    EXPECT_TRUE(dw::file::exists(tmp.path()));
    EXPECT_TRUE(dw::file::isDirectory(tmp.path()));
    EXPECT_FALSE(dw::file::isFile(tmp.path()));
}

TEST(FileUtils, Exists_NonExistent) {
    EXPECT_FALSE(dw::file::exists("/nonexistent/file"));
}

// --- createDirectory / createDirectories ---

TEST(FileUtils, CreateDirectory_Single) {
    TempDir tmp;
    auto dir = tmp / "subdir";
    ASSERT_TRUE(dw::file::createDirectory(dir));
    EXPECT_TRUE(dw::file::isDirectory(dir));
}

TEST(FileUtils, CreateDirectories_Nested) {
    TempDir tmp;
    auto dir = tmp / "a" / "b" / "c";
    ASSERT_TRUE(dw::file::createDirectories(dir));
    EXPECT_TRUE(dw::file::isDirectory(dir));
}

// --- remove ---

TEST(FileUtils, Remove_File) {
    TempDir tmp;
    auto path = tmp / "removeme.txt";
    ASSERT_TRUE(dw::file::writeText(path, "x"));
    ASSERT_TRUE(dw::file::exists(path));

    EXPECT_TRUE(dw::file::remove(path));
    EXPECT_FALSE(dw::file::exists(path));
}

// --- copy / move ---

TEST(FileUtils, Copy_File) {
    TempDir tmp;
    auto src = tmp / "src.txt";
    auto dst = tmp / "dst.txt";
    ASSERT_TRUE(dw::file::writeText(src, "content"));

    ASSERT_TRUE(dw::file::copy(src, dst));
    EXPECT_TRUE(dw::file::exists(src));
    EXPECT_TRUE(dw::file::exists(dst));

    auto result = dw::file::readText(dst);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "content");
}

TEST(FileUtils, Move_File) {
    TempDir tmp;
    auto src = tmp / "src.txt";
    auto dst = tmp / "dst.txt";
    ASSERT_TRUE(dw::file::writeText(src, "content"));

    ASSERT_TRUE(dw::file::move(src, dst));
    EXPECT_FALSE(dw::file::exists(src));
    EXPECT_TRUE(dw::file::exists(dst));
}

// --- getFileSize ---

TEST(FileUtils, GetFileSize_NonZero) {
    TempDir tmp;
    auto path = tmp / "size.txt";
    ASSERT_TRUE(dw::file::writeText(path, "hello"));

    auto result = dw::file::getFileSize(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5u);
}

// --- listFiles ---

TEST(FileUtils, ListFiles_Basic) {
    TempDir tmp;
    ASSERT_TRUE(dw::file::writeText(tmp / "a.txt", "a"));
    ASSERT_TRUE(dw::file::writeText(tmp / "b.stl", "b"));
    ASSERT_TRUE(dw::file::writeText(tmp / "c.txt", "c"));

    auto all = dw::file::listFiles(tmp.path());
    EXPECT_GE(all.size(), 3u);
}

TEST(FileUtils, ListFiles_FilteredByExtension) {
    TempDir tmp;
    ASSERT_TRUE(dw::file::writeText(tmp / "a.txt", "a"));
    ASSERT_TRUE(dw::file::writeText(tmp / "b.stl", "b"));
    ASSERT_TRUE(dw::file::writeText(tmp / "c.txt", "c"));

    auto stls = dw::file::listFiles(tmp.path(), "stl");
    EXPECT_EQ(stls.size(), 1u);
}
