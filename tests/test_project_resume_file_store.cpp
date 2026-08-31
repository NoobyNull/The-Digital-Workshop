#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/project_resume_file_store.h"
#include "core/utils/file_utils.h"

namespace dw {
namespace {

Path uniqueResumeTestRoot() {
    static std::atomic<unsigned long long> sequence{0};
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("dw-project-resume-store-" + std::to_string(now) + "-" +
            std::to_string(sequence.fetch_add(1)));
}

class ProjectResumeFileStoreTest : public ::testing::Test {
  protected:
    void SetUp() override {
        root = uniqueResumeTestRoot();
        ASSERT_TRUE(file::createDirectories(root));
        resumePath = root / "resume.json";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    Path root;
    Path resumePath;
};

TEST_F(ProjectResumeFileStoreTest, RoundTripsAndAtomicallyReplacesBookmark) {
    ProjectResumeFileStore store(resumePath);
    const workshop::ProjectResumeBookmark first{workshop::ProjectId(42),
                                                workshop::ProjectItemId(7)};

    ASSERT_TRUE(store.save(first));
    ASSERT_TRUE(file::isFile(resumePath));
    const auto loadedFirst = store.load();
    ASSERT_EQ(loadedFirst.status, workshop::ProjectResumeLoadStatus::Loaded);
    ASSERT_TRUE(loadedFirst.bookmark.has_value());
    EXPECT_EQ(loadedFirst.bookmark->project, workshop::ProjectId(42));
    ASSERT_TRUE(loadedFirst.bookmark->item.has_value());
    EXPECT_EQ(*loadedFirst.bookmark->item, workshop::ProjectItemId(7));

    const auto serialized = file::readText(resumePath);
    ASSERT_TRUE(serialized.has_value());
    const auto document = nlohmann::json::parse(*serialized);
    EXPECT_EQ(document.at("version"), 1);
    EXPECT_EQ(document.at("project_id"), 42);
    EXPECT_EQ(document.at("project_item_id"), 7);

    ASSERT_TRUE(store.save(workshop::ProjectResumeBookmark{workshop::ProjectId(99), std::nullopt}));
    const auto loadedSecond = store.load();
    ASSERT_EQ(loadedSecond.status, workshop::ProjectResumeLoadStatus::Loaded);
    ASSERT_TRUE(loadedSecond.bookmark.has_value());
    EXPECT_EQ(loadedSecond.bookmark->project, workshop::ProjectId(99));
    EXPECT_FALSE(loadedSecond.bookmark->item.has_value());

    const auto entries = file::listEntries(root);
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front(), resumePath.filename().string());
}

TEST_F(ProjectResumeFileStoreTest, ClearRemovesBookmarkAndIsIdempotent) {
    ProjectResumeFileStore store(resumePath);
    ASSERT_TRUE(store.save(workshop::ProjectResumeBookmark{workshop::ProjectId(42), std::nullopt}));

    EXPECT_TRUE(store.clear());
    EXPECT_FALSE(file::exists(resumePath));
    EXPECT_EQ(store.load().status, workshop::ProjectResumeLoadStatus::Missing);
    EXPECT_TRUE(store.clear());
}

TEST_F(ProjectResumeFileStoreTest, CorruptOrUnsupportedDocumentsAreIgnored) {
    ProjectResumeFileStore store(resumePath);

    ASSERT_TRUE(file::writeText(resumePath, "{not-json"));
    EXPECT_EQ(store.load().status, workshop::ProjectResumeLoadStatus::Invalid);

    ASSERT_TRUE(
        file::writeText(resumePath, R"({"version":2,"project_id":42,"project_item_id":7})"));
    EXPECT_EQ(store.load().status, workshop::ProjectResumeLoadStatus::Invalid);

    ASSERT_TRUE(file::writeText(resumePath, R"([{"version":1,"project_id":42}])"));
    EXPECT_EQ(store.load().status, workshop::ProjectResumeLoadStatus::Invalid);
}

TEST_F(ProjectResumeFileStoreTest, MalformedIdentifiersCannotBecomeBookmarks) {
    ProjectResumeFileStore store(resumePath);
    const std::vector<std::string> malformedDocuments = {
        R"({"version":1})",
        R"({"version":1,"project_id":0})",
        R"({"version":1,"project_id":-1})",
        R"({"version":1,"project_id":"42"})",
        R"({"version":1,"project_id":42.5})",
        R"({"version":1,"project_id":9223372036854775808})",
        R"({"version":1,"project_id":42,"project_item_id":0})",
        R"({"version":1,"project_id":42,"project_item_id":-7})",
        R"({"version":1,"project_id":42,"project_item_id":"7"})",
    };

    for (const auto& document : malformedDocuments) {
        SCOPED_TRACE(document);
        ASSERT_TRUE(file::writeText(resumePath, document));
        EXPECT_EQ(store.load().status, workshop::ProjectResumeLoadStatus::Invalid);
    }
}

TEST_F(ProjectResumeFileStoreTest, InvalidBookmarkIsRejectedBeforePersistence) {
    ProjectResumeFileStore store(resumePath);

    EXPECT_FALSE(store.save(workshop::ProjectResumeBookmark{}));
    EXPECT_FALSE(file::exists(resumePath));

    ASSERT_TRUE(store.save(
        workshop::ProjectResumeBookmark{workshop::ProjectId(42), workshop::ProjectItemId(7)}));
    EXPECT_FALSE(store.save(
        workshop::ProjectResumeBookmark{workshop::ProjectId(42), workshop::ProjectItemId(-7)}));
    const auto retained = store.load();
    ASSERT_EQ(retained.status, workshop::ProjectResumeLoadStatus::Loaded);
    ASSERT_TRUE(retained.bookmark.has_value());
    EXPECT_EQ(retained.bookmark->project, workshop::ProjectId(42));
    EXPECT_EQ(retained.bookmark->item, workshop::ProjectItemId(7));
}

TEST_F(ProjectResumeFileStoreTest, PersistenceFailureIsReturnedWithoutTempResidue) {
    ASSERT_TRUE(file::createDirectories(resumePath));
    ASSERT_TRUE(file::writeText(resumePath / "owned", "keep"));
    ProjectResumeFileStore store(resumePath);

    EXPECT_EQ(store.load().status, workshop::ProjectResumeLoadStatus::Invalid);

    EXPECT_FALSE(
        store.save(workshop::ProjectResumeBookmark{workshop::ProjectId(42), std::nullopt}));
    EXPECT_FALSE(store.clear());
    EXPECT_TRUE(file::isDirectory(resumePath));

    const auto entries = file::listEntries(root);
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front(), resumePath.filename().string());
}

TEST_F(ProjectResumeFileStoreTest, CoordinatorRepairsCorruptSidecarAndReportsFailedRepair) {
    ProjectResumeFileStore store(resumePath);
    ASSERT_TRUE(file::writeText(resumePath, "{not-json"));
    int homeShows = 0;
    workshop::ProjectResumeCallbacks callbacks;
    callbacks.showHome = [&homeShows]() { ++homeShows; };
    workshop::ProjectResumeCoordinator coordinator(store, callbacks);

    const auto repaired = coordinator.restore();

    EXPECT_EQ(repaired.status, workshop::ProjectResumeStatus::InvalidBookmark);
    EXPECT_TRUE(repaired.persistenceHealthy);
    EXPECT_FALSE(file::exists(resumePath));
    EXPECT_EQ(homeShows, 1);

    ASSERT_TRUE(file::createDirectories(resumePath));
    ASSERT_TRUE(file::writeText(resumePath / "owned", "keep"));
    const auto failedRepair = coordinator.restore();
    EXPECT_EQ(failedRepair.status, workshop::ProjectResumeStatus::PersistenceFailure);
    EXPECT_FALSE(failedRepair.persistenceHealthy);
    EXPECT_TRUE(file::isDirectory(resumePath));
    EXPECT_EQ(homeShows, 2);
}

} // namespace
} // namespace dw
