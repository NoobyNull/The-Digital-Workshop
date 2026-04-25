// Digital Workshop - Import Pipeline Tests

#include <gtest/gtest.h>

#include "core/database/connection_pool.h"
#include "core/database/database.h"
#include "core/database/model_repository.h"
#include "core/database/schema.h"
#include "core/import/file_handler.h"
#include "core/import/import_queue.h"
#include "core/import/import_task.h"
#include "core/utils/file_utils.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

// --- ImportProgress pure function tests ---

TEST(ImportProgress, PercentComplete_Zero) {
    dw::ImportProgress progress;
    EXPECT_EQ(progress.percentComplete(), 0);
}

TEST(ImportProgress, PercentComplete_Half) {
    dw::ImportProgress progress;
    progress.totalFiles.store(10);
    progress.completedFiles.store(5);
    EXPECT_EQ(progress.percentComplete(), 50);
}

TEST(ImportProgress, PercentComplete_Full) {
    dw::ImportProgress progress;
    progress.totalFiles.store(4);
    progress.completedFiles.store(4);
    EXPECT_EQ(progress.percentComplete(), 100);
}

TEST(ImportProgress, Reset) {
    dw::ImportProgress progress;
    progress.totalFiles.store(10);
    progress.completedFiles.store(5);
    progress.failedFiles.store(2);
    progress.active.store(true);

    progress.reset();

    EXPECT_EQ(progress.totalFiles.load(), 0);
    EXPECT_EQ(progress.completedFiles.load(), 0);
    EXPECT_EQ(progress.failedFiles.load(), 0);
    EXPECT_FALSE(progress.active.load());
}

// --- importStageName ---

TEST(ImportStageName, AllStages) {
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Pending), "Queued");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Reading), "Reading file");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Hashing), "Computing hash");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::CheckingDuplicate), "Checking duplicates");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Parsing), "Parsing mesh");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Inserting), "Saving to library");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::WaitingForThumbnail), "Generating thumbnail");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Done), "Done");
    EXPECT_STREQ(dw::importStageName(dw::ImportStage::Failed), "Failed");
}

// --- ImportTask default state ---

TEST(ImportTask, DefaultState) {
    dw::ImportTask task;
    EXPECT_EQ(task.stage, dw::ImportStage::Pending);
    EXPECT_TRUE(task.error.empty());
    EXPECT_FALSE(task.isDuplicate);
    EXPECT_EQ(task.modelId, 0);
    EXPECT_EQ(task.mesh, nullptr);
}

// --- ImportQueue integration test ---

namespace {

class ImportQueueTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create temporary database file for connection pool
        m_tmpDir = std::filesystem::temp_directory_path() / "dw_test_import";
        std::filesystem::create_directories(m_tmpDir);
        m_dbPath = m_tmpDir / "test.db";

        // Initialize database and schema
        dw::Database db;
        ASSERT_TRUE(db.open(m_dbPath));
        ASSERT_TRUE(dw::Schema::initialize(db));
        db.close();

        // Create connection pool (2 connections)
        m_pool = std::make_unique<dw::ConnectionPool>(m_dbPath, 2);
    }

    void TearDown() override {
        m_pool.reset();
        std::filesystem::remove_all(m_tmpDir);
    }

    dw::Path writeMiniSTL(const std::string& name) {
        auto path = m_tmpDir / (name + ".stl");
        dw::ByteBuffer buf(80 + 4 + 50, 0);
        dw::u32 triCount = 1;
        std::memcpy(buf.data() + 80, &triCount, sizeof(triCount));
        float tri[12] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0};
        std::memcpy(buf.data() + 84, tri, sizeof(tri));
        EXPECT_TRUE(dw::file::writeBinary(path, buf));
        return path;
    }

    std::unique_ptr<dw::ConnectionPool> m_pool;
    dw::Path m_dbPath;
    std::filesystem::path m_tmpDir;
};

} // namespace

TEST_F(ImportQueueTest, EnqueueAndProcess) {
    auto stlPath = writeMiniSTL("test_model");
    dw::ImportQueue queue(*m_pool);

    queue.enqueue(stlPath);

    // Wait for worker to finish (with timeout)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (queue.isActive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_FALSE(queue.isActive()) << "Queue did not finish in time";

    // Poll completed tasks
    auto completed = queue.pollCompleted();
    ASSERT_EQ(completed.size(), 1u);
    EXPECT_EQ(completed[0].stage, dw::ImportStage::WaitingForThumbnail);
    EXPECT_GT(completed[0].modelId, 0);
    EXPECT_NE(completed[0].mesh, nullptr);

    // Verify model is in database (use pooled connection)
    dw::ScopedConnection conn(*m_pool);
    dw::ModelRepository repo(*conn);
    auto model = repo.findById(completed[0].modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->name, "test_model");
    EXPECT_EQ(model->fileFormat, "stl");
}

TEST_F(ImportQueueTest, DuplicateRejected) {
    auto stlPath = writeMiniSTL("dup_test");
    dw::ImportQueue queue(*m_pool);

    // First import
    queue.enqueue(stlPath);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (queue.isActive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    queue.pollCompleted();

    // Second import of same file
    queue.enqueue(stlPath);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (queue.isActive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Duplicates go to batchSummary, not pollCompleted
    EXPECT_EQ(queue.batchSummary().duplicateCount, 1);

    // Database should still have only 1 model
    dw::ScopedConnection conn(*m_pool);
    dw::ModelRepository repo(*conn);
    EXPECT_EQ(repo.count(), 1);
}

TEST_F(ImportQueueTest, Progress_TracksCorrectly) {
    auto stlPath = writeMiniSTL("progress_test");
    dw::ImportQueue queue(*m_pool);

    queue.enqueue(stlPath);
    EXPECT_EQ(queue.progress().totalFiles.load(), 1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (queue.isActive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(queue.progress().completedFiles.load(), 1);
    EXPECT_FALSE(queue.isActive());
}

TEST(FileHandlerTest, EnsureLibraryDirAcceptsExistingDirectory) {
    auto tmpDir = std::filesystem::temp_directory_path() / "dw_test_file_handler_existing";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    EXPECT_TRUE(dw::FileHandler::ensureLibraryDir(tmpDir));

    std::filesystem::remove_all(tmpDir);
}

TEST(FileHandlerTest, CopyToExistingLibraryDirectory) {
    auto tmpDir = std::filesystem::temp_directory_path() / "dw_test_file_handler_copy";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir / "library");

    auto source = tmpDir / "source.stl";
    ASSERT_TRUE(dw::file::writeText(source, "solid test\nendsolid test\n"));

    std::string error;
    auto handledPath = dw::FileHandler::handleImportedFile(
        source, dw::FileHandlingMode::CopyToLibrary, tmpDir / "library", error);

    EXPECT_FALSE(handledPath.empty()) << error;
    EXPECT_TRUE(dw::file::exists(source));
    EXPECT_TRUE(dw::file::exists(handledPath));

    std::filesystem::remove_all(tmpDir);
}
