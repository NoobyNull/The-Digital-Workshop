#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/project/named_project_creation.h"
#include "core/project/project.h"
#include "core/utils/file_utils.h"

namespace {

class NamedProjectCreationTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_root = std::filesystem::temp_directory_path() /
                 ("dw_named_project_creation_" +
                  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);

        m_oldProjectsDir = dw::Config::instance().getProjectsDir();
        dw::Config::instance().setProjectsDir(m_root / "Projects");
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_database));
        m_manager = std::make_unique<dw::ProjectManager>(m_database);
        m_service = std::make_unique<dw::NamedProjectCreationService>(*m_manager);
    }

    void TearDown() override {
        m_service.reset();
        m_manager.reset();
        dw::Config::instance().setProjectsDir(m_oldProjectsDir);
        std::filesystem::remove_all(m_root);
    }

    dw::Path m_root;
    dw::Path m_oldProjectsDir;
    dw::Database m_database;
    std::unique_ptr<dw::ProjectManager> m_manager;
    std::unique_ptr<dw::NamedProjectCreationService> m_service;
};

TEST_F(NamedProjectCreationTest, PrepareCreatesOwnedTemporaryProjectAndToken) {
    const auto result = m_service->prepare("  River Sign  ");

    ASSERT_TRUE(result.prepared());
    EXPECT_EQ(result.project->name(), "River Sign");
    EXPECT_TRUE(result.project->isTemporary());
    EXPECT_EQ(result.token.projectId, result.project->id());
    EXPECT_EQ(result.token.root, result.project->filePath());
    EXPECT_TRUE(dw::file::exists(result.token.root / "project.json"));
    EXPECT_TRUE(dw::file::exists(result.token.root / ".dw-temporary-project"));
}

TEST_F(NamedProjectCreationTest, InvalidNameFailsBeforeCreatingRecord) {
    const auto result = m_service->prepare("   ");

    EXPECT_EQ(result.status, dw::NamedProjectPrepareStatus::CreateFailed);
    EXPECT_FALSE(result.project);
    EXPECT_FALSE(result.token.valid());
    EXPECT_TRUE(m_manager->listProjects().empty());
}

TEST_F(NamedProjectCreationTest, StorageFailureRemovesPreparedDatabaseRecord) {
    const dw::Path blockedProjectsRoot = m_root / "blocked-projects-root";
    ASSERT_TRUE(dw::file::writeText(blockedProjectsRoot, "not a directory"));
    dw::Config::instance().setProjectsDir(blockedProjectsRoot);

    const auto result = m_service->prepare("Cannot Be Stored");

    EXPECT_EQ(result.status, dw::NamedProjectPrepareStatus::StorageFailed);
    EXPECT_FALSE(result.project);
    EXPECT_TRUE(m_manager->listProjects().empty());
}

TEST_F(NamedProjectCreationTest, AcceptedActivationPublishesOnlyMatchingActiveProject) {
    const auto prepared = m_service->prepare("River Sign");
    ASSERT_TRUE(prepared.prepared());
    const dw::Path temporaryRoot = prepared.token.root;
    m_manager->synchronizeActiveProject(prepared.project);

    const auto status = m_service->finish(prepared.token, true);

    EXPECT_EQ(status, dw::NamedProjectFinishStatus::Published);
    ASSERT_TRUE(m_manager->currentProject());
    EXPECT_FALSE(m_manager->currentProject()->isTemporary());
    EXPECT_NE(m_manager->currentProject()->filePath(), temporaryRoot);
    EXPECT_FALSE(dw::file::exists(temporaryRoot));
    EXPECT_TRUE(dw::file::exists(m_manager->currentProject()->filePath() / "project.json"));
}

TEST_F(NamedProjectCreationTest, PublishFailureLeavesActiveTemporaryProjectMarkedForSaving) {
    const auto prepared = m_service->prepare("Retry Saving");
    ASSERT_TRUE(prepared.prepared());
    m_manager->synchronizeActiveProject(prepared.project);
    ASSERT_TRUE(dw::file::remove(prepared.token.root / ".dw-temporary-project"));

    const auto status = m_service->finish(prepared.token, true);

    EXPECT_EQ(status, dw::NamedProjectFinishStatus::NeedsSaving);
    ASSERT_TRUE(m_manager->currentProject());
    EXPECT_EQ(m_manager->currentProject()->id(), prepared.token.projectId);
    EXPECT_TRUE(m_manager->currentProject()->isTemporary());
    EXPECT_TRUE(m_manager->currentProject()->isModified());
    EXPECT_TRUE(dw::file::exists(prepared.token.root / "project.json"));
}

TEST_F(NamedProjectCreationTest, RejectedActivationDeletesOnlyOwnedPreparedProject) {
    const auto prepared = m_service->prepare("Rejected Project");
    ASSERT_TRUE(prepared.prepared());

    const auto status = m_service->finish(prepared.token, false);

    EXPECT_EQ(status, dw::NamedProjectFinishStatus::RejectedCleaned);
    EXPECT_FALSE(m_manager->getProjectInfo(prepared.token.projectId).has_value());
    EXPECT_FALSE(dw::file::exists(prepared.token.root));
}

TEST_F(NamedProjectCreationTest, RejectedActivationReportsCleanupFailureWithoutDeletingUnownedData) {
    const auto prepared = m_service->prepare("Unowned Rejection");
    ASSERT_TRUE(prepared.prepared());
    ASSERT_TRUE(dw::file::remove(prepared.token.root / ".dw-temporary-project"));

    const auto status = m_service->finish(prepared.token, false);

    EXPECT_EQ(status, dw::NamedProjectFinishStatus::CleanupFailed);
    EXPECT_TRUE(m_manager->getProjectInfo(prepared.token.projectId).has_value());
    EXPECT_TRUE(dw::file::exists(prepared.token.root / "project.json"));
}

TEST_F(NamedProjectCreationTest, AcceptedActivationNeverPublishesDifferentActiveIdentity) {
    const auto prepared = m_service->prepare("Prepared Candidate");
    ASSERT_TRUE(prepared.prepared());

    auto other = m_manager->create("Other Project");
    ASSERT_TRUE(other);
    ASSERT_TRUE(m_manager->save(*other));
    m_manager->synchronizeActiveProject(other);

    const auto status = m_service->finish(prepared.token, true);

    EXPECT_EQ(status, dw::NamedProjectFinishStatus::ActiveIdentityMismatch);
    ASSERT_TRUE(m_manager->currentProject());
    EXPECT_EQ(m_manager->currentProject()->id(), other->id());
    const auto candidate = m_manager->getProjectInfo(prepared.token.projectId);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_TRUE(candidate->temporary);
    EXPECT_TRUE(dw::file::exists(prepared.token.root / ".dw-temporary-project"));
}

TEST_F(NamedProjectCreationTest, RejectedResultCannotDeleteCandidateThatIsActuallyActive) {
    const auto prepared = m_service->prepare("Still Active");
    ASSERT_TRUE(prepared.prepared());
    m_manager->synchronizeActiveProject(prepared.project);

    const auto status = m_service->finish(prepared.token, false);

    EXPECT_EQ(status, dw::NamedProjectFinishStatus::ActiveIdentityMismatch);
    EXPECT_TRUE(m_manager->getProjectInfo(prepared.token.projectId).has_value());
    EXPECT_TRUE(dw::file::exists(prepared.token.root / ".dw-temporary-project"));
}

} // namespace
