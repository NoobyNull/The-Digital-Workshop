// Digital Workshop - Library Manager Tests

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/schema.h"
#include "core/library/library_manager.h"
#include "core/utils/file_utils.h"

#include <cstring>
#include <filesystem>

namespace {

class LibraryManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
        m_mgr = std::make_unique<dw::LibraryManager>(m_db);

        m_tmpDir = std::filesystem::temp_directory_path() / "dw_test_libmgr";
        std::filesystem::create_directories(m_tmpDir);
    }

    void TearDown() override { std::filesystem::remove_all(m_tmpDir); }

    // Write a minimal binary STL (1 triangle) to disk
    dw::Path writeMiniSTL(const std::string& name) {
        auto path = m_tmpDir / (name + ".stl");

        dw::ByteBuffer buf(80 + 4 + 50, 0);
        dw::u32 triCount = 1;
        std::memcpy(buf.data() + 80, &triCount, sizeof(triCount));

        // Write one triangle with non-degenerate vertices
        float tri[12] = {
            0,
            0,
            1, // normal
            0,
            0,
            0, // v0
            1,
            0,
            0, // v1
            0,
            1,
            0 // v2
        };
        std::memcpy(buf.data() + 84, tri, sizeof(tri));

        EXPECT_TRUE(dw::file::writeBinary(path, buf));
        return path;
    }

    // Write a different STL (different content → different hash)
    dw::Path writeDifferentSTL(const std::string& name) {
        auto path = m_tmpDir / (name + ".stl");

        dw::ByteBuffer buf(80 + 4 + 50, 0);
        dw::u32 triCount = 1;
        std::memcpy(buf.data() + 80, &triCount, sizeof(triCount));

        float tri[12] = {0,
                         0,
                         1,
                         0,
                         0,
                         0,
                         2,
                         0,
                         0, // different vertex → different hash
                         0,
                         2,
                         0};
        std::memcpy(buf.data() + 84, tri, sizeof(tri));

        EXPECT_TRUE(dw::file::writeBinary(path, buf));
        return path;
    }

    dw::Database m_db;
    std::unique_ptr<dw::LibraryManager> m_mgr;
    std::filesystem::path m_tmpDir;
};

} // namespace

// --- Import ---

TEST_F(LibraryManagerTest, ImportModel_Success) {
    auto path = writeMiniSTL("cube");
    auto result = m_mgr->importModel(path);
    EXPECT_TRUE(result.success) << "Error: " << result.error;
    EXPECT_GT(result.modelId, 0);
    EXPECT_FALSE(result.isDuplicate);
}

TEST_F(LibraryManagerTest, ImportModel_DuplicateDetected) {
    auto path = writeMiniSTL("cube");
    auto r1 = m_mgr->importModel(path);
    ASSERT_TRUE(r1.success) << r1.error;

    // Import same file again — should detect duplicate
    auto r2 = m_mgr->importModel(path);
    EXPECT_TRUE(r2.isDuplicate);
}

TEST_F(LibraryManagerTest, ImportModel_NonExistent) {
    auto result = m_mgr->importModel("/nonexistent/model.stl");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(LibraryManagerTest, ImportModel_UnsupportedFormat) {
    auto path = m_tmpDir / "model.fbx";
    ASSERT_TRUE(dw::file::writeText(path, "not a real fbx"));
    auto result = m_mgr->importModel(path);
    EXPECT_FALSE(result.success);
}

// --- Query ---

TEST_F(LibraryManagerTest, GetAllModels_Empty) {
    auto models = m_mgr->getAllModels();
    EXPECT_TRUE(models.empty());
}

TEST_F(LibraryManagerTest, GetAllModels_AfterImport) {
    writeMiniSTL("a");
    writeDifferentSTL("b");
    m_mgr->importModel(m_tmpDir / "a.stl");
    m_mgr->importModel(m_tmpDir / "b.stl");

    auto models = m_mgr->getAllModels();
    EXPECT_EQ(models.size(), 2u);
}

TEST_F(LibraryManagerTest, UpdateTagStatus_PersistsStatus) {
    auto path = writeMiniSTL("taggable");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    ASSERT_TRUE(m_mgr->updateTagStatus(result.modelId, 2));

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 2);
}

TEST_F(LibraryManagerTest, SearchModels) {
    auto pathA = writeMiniSTL("widget_bracket");
    m_mgr->importModel(pathA);

    auto pathB = writeDifferentSTL("gear_shaft");
    m_mgr->importModel(pathB);

    auto results = m_mgr->searchModels("widget");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "widget_bracket");
}

TEST_F(LibraryManagerTest, GetModel_ById) {
    auto path = writeMiniSTL("mymodel");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->name, "mymodel");
    EXPECT_EQ(model->fileFormat, "stl");
    EXPECT_GT(model->vertexCount, 0u);
}

// --- Update ---

TEST_F(LibraryManagerTest, UpdateTags) {
    auto path = writeMiniSTL("tagged");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);

    EXPECT_TRUE(m_mgr->updateTags(result.modelId, {"cnc", "bracket"}));

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tags.size(), 2u);
}

// --- Remove ---

TEST_F(LibraryManagerTest, RemoveModel) {
    auto path = writeMiniSTL("removable");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(m_mgr->modelCount(), 1);

    EXPECT_TRUE(m_mgr->removeModel(result.modelId));
    EXPECT_EQ(m_mgr->modelCount(), 0);
}

// --- ModelExists ---

TEST_F(LibraryManagerTest, ModelExists) {
    auto path = writeMiniSTL("exists_test");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(m_mgr->modelExists(model->hash));
    EXPECT_FALSE(m_mgr->modelExists("nonexistent_hash"));
}

// --- ModelCount ---

TEST_F(LibraryManagerTest, ModelCount) {
    EXPECT_EQ(m_mgr->modelCount(), 0);
    m_mgr->importModel(writeMiniSTL("a"));
    EXPECT_EQ(m_mgr->modelCount(), 1);
    m_mgr->importModel(writeDifferentSTL("b"));
    EXPECT_EQ(m_mgr->modelCount(), 2);
}
