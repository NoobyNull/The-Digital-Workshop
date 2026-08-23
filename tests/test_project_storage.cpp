#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/mesh/hash.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/utils/file_utils.h"

namespace {

class ProjectStorageTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_root = std::filesystem::temp_directory_path() /
                 ("dw_project_storage_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);

        m_oldProjectsDir = dw::Config::instance().getProjectsDir();
        dw::Config::instance().setProjectsDir(m_root / "Projects");
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_database));
        m_manager = std::make_unique<dw::ProjectManager>(m_database);
    }

    void TearDown() override {
        m_manager.reset();
        dw::Config::instance().setProjectsDir(m_oldProjectsDir);
        std::filesystem::remove_all(m_root);
    }

    dw::Path writeModel(const std::string& name) {
        const dw::Path path = m_root / name;
        EXPECT_TRUE(dw::file::writeText(path,
                                        "solid part\n"
                                        "facet normal 0 0 1\n"
                                        "outer loop\n"
                                        "vertex 0 0 0\n"
                                        "vertex 1 0 0\n"
                                        "vertex 0 1 0\n"
                                        "endloop\n"
                                        "endfacet\n"
                                        "endsolid part\n"));
        return path;
    }

    dw::Path m_root;
    dw::Path m_oldProjectsDir;
    dw::Database m_database;
    std::unique_ptr<dw::ProjectManager> m_manager;
};

TEST_F(ProjectStorageTest, TemporaryDirectoryCannotCollideWithPermanentProject) {
    auto permanent = m_manager->create("River Sign");
    ASSERT_TRUE(permanent);
    ASSERT_TRUE(m_manager->save(*permanent));
    const dw::Path permanentRoot = permanent->filePath();
    const dw::Path sentinel = permanentRoot / "keep.txt";
    ASSERT_TRUE(dw::file::writeText(sentinel, "permanent"));

    auto temporary = m_manager->create("River Sign", true);
    ASSERT_TRUE(temporary);
    ASSERT_TRUE(m_manager->save(*temporary));

    EXPECT_NE(temporary->filePath(), permanentRoot);
    EXPECT_EQ(temporary->filePath().parent_path().filename(), ".temporary");
    EXPECT_TRUE(dw::file::exists(sentinel));
    EXPECT_TRUE(m_manager->discardTemporaryProjectData(temporary->id(), temporary->filePath()));
    EXPECT_TRUE(dw::file::exists(permanentRoot / "project.json"));
    EXPECT_TRUE(dw::file::exists(sentinel));
}

TEST_F(ProjectStorageTest, ManifestOwnerPreventsAnotherRecordFromReusingDirectory) {
    auto owner = m_manager->create("Owned Project");
    ASSERT_TRUE(owner);
    ASSERT_TRUE(m_manager->save(*owner));
    const dw::Path ownerRoot = owner->filePath();

    auto intruder = m_manager->create("Intruder");
    ASSERT_TRUE(intruder);
    intruder->setFilePath(ownerRoot);
    intruder->markModified();

    EXPECT_FALSE(m_manager->save(*intruder));
    EXPECT_TRUE(intruder->isModified());

    dw::ProjectDirectory directory;
    ASSERT_TRUE(directory.open(ownerRoot));
    EXPECT_EQ(directory.projectId(), owner->id());
    EXPECT_EQ(directory.name(), owner->name());
}

TEST_F(ProjectStorageTest, SaveFailureKeepsDirtyStateAndRollsBackAssociations) {
    auto project = m_manager->create("Broken Association");
    ASSERT_TRUE(project);
    project->addModel(999999);

    EXPECT_FALSE(m_manager->save(*project));
    EXPECT_TRUE(project->isModified());
    EXPECT_TRUE(project->filePath().empty());
    EXPECT_EQ(m_manager->currentDirectory(), nullptr);

    auto reopened = m_manager->open(project->id());
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->modelIds().empty());
}

TEST_F(ProjectStorageTest, PromotionPreservesRegisteredModelManifestEntry) {
    const dw::Path source = writeModel("river-sign.stl");
    dw::ModelRecord record;
    record.name = "River Sign";
    record.hash = dw::hash::computeFile(source);
    record.filePath = source;
    record.fileFormat = "stl";
    dw::ModelRepository models(m_database);
    const auto modelId = models.insert(record);
    ASSERT_TRUE(modelId.has_value());

    auto project = m_manager->create("River Sign Carve", true);
    ASSERT_TRUE(project);
    project->addModel(*modelId);
    ASSERT_TRUE(m_manager->save(*project));
    m_manager->synchronizeActiveProject(project);

    dw::ProjectDirectory temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.open(project->filePath()));
    ASSERT_EQ(temporaryDirectory.models().size(), 1u);
    const dw::Path temporaryRoot = project->filePath();

    ASSERT_TRUE(m_manager->saveTemporaryProject());
    EXPECT_FALSE(dw::file::exists(temporaryRoot));

    dw::ProjectDirectory permanentDirectory;
    ASSERT_TRUE(permanentDirectory.open(project->filePath()));
    ASSERT_EQ(permanentDirectory.models().size(), 1u);
    EXPECT_EQ(permanentDirectory.models().front().hash, record.hash);
}

TEST_F(ProjectStorageTest, FailedDatabaseDiscardLeavesOwnedDirectoryUntouched) {
    auto project = m_manager->create("Recoverable Temporary", true);
    ASSERT_TRUE(project);
    ASSERT_TRUE(m_manager->save(*project));
    m_manager->synchronizeActiveProject(project);
    const dw::Path temporaryRoot = project->filePath();

    m_database.close();
    EXPECT_FALSE(m_manager->discardTemporaryProjectData(project->id(), temporaryRoot));
    EXPECT_TRUE(dw::file::exists(temporaryRoot / "project.json"));
    EXPECT_TRUE(dw::file::exists(temporaryRoot / ".dw-temporary-project"));
}

TEST_F(ProjectStorageTest, TemporaryOwnershipSurvivesDatabaseReopen) {
    auto project = m_manager->create("Restart Safe Temporary", true);
    ASSERT_TRUE(project);
    ASSERT_TRUE(m_manager->save(*project));
    const dw::i64 projectId = project->id();
    const dw::Path projectRoot = project->filePath();

    auto reopened = m_manager->open(projectId);
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->isTemporary());
    EXPECT_EQ(reopened->filePath(), projectRoot);

    m_manager->synchronizeActiveProject(reopened);
    ASSERT_TRUE(m_manager->currentDirectory());
    EXPECT_TRUE(m_manager->discardTemporaryProjectData());
    EXPECT_FALSE(dw::file::exists(projectRoot));
}

TEST_F(ProjectStorageTest, StorageValidatorAcceptsOwnedPermanentAndTemporaryProjects) {
    auto permanent = m_manager->create("Permanent Project");
    ASSERT_TRUE(permanent);
    ASSERT_TRUE(m_manager->save(*permanent));
    EXPECT_EQ(m_manager->validateProjectStorage(permanent->id()),
              dw::ProjectStorageValidationStatus::Ready);

    auto temporary = m_manager->create("Temporary Project", true);
    ASSERT_TRUE(temporary);
    ASSERT_TRUE(m_manager->save(*temporary));
    EXPECT_EQ(m_manager->validateProjectStorage(temporary->id()),
              dw::ProjectStorageValidationStatus::Ready);
}

TEST_F(ProjectStorageTest, StorageValidatorDistinguishesMissingRecordAndPath) {
    EXPECT_EQ(m_manager->validateProjectStorage(999999),
              dw::ProjectStorageValidationStatus::MissingRecord);

    auto project = m_manager->create("No Storage Yet");
    ASSERT_TRUE(project);
    EXPECT_TRUE(project->filePath().empty());
    EXPECT_EQ(m_manager->validateProjectStorage(project->id()),
              dw::ProjectStorageValidationStatus::MissingPath);

    dw::ProjectRepository projects(m_database);
    project->setFilePath(m_root / "does-not-exist");
    ASSERT_TRUE(projects.update(project->record()));
    EXPECT_EQ(m_manager->validateProjectStorage(project->id()),
              dw::ProjectStorageValidationStatus::MissingPath);
}

TEST_F(ProjectStorageTest, StorageValidatorRejectsExistingNonProjectDirectory) {
    auto project = m_manager->create("Invalid Storage");
    ASSERT_TRUE(project);
    const dw::Path invalidRoot = m_root / "invalid-storage";
    ASSERT_TRUE(dw::file::createDirectories(invalidRoot));

    dw::ProjectRepository projects(m_database);
    project->setFilePath(invalidRoot);
    ASSERT_TRUE(projects.update(project->record()));

    EXPECT_EQ(m_manager->validateProjectStorage(project->id()),
              dw::ProjectStorageValidationStatus::InvalidDirectory);
}

TEST_F(ProjectStorageTest, StorageValidatorRejectsManifestIdentityMismatch) {
    auto project = m_manager->create("Identity Mismatch");
    ASSERT_TRUE(project);
    ASSERT_TRUE(m_manager->save(*project));
    ASSERT_TRUE(dw::file::writeTextAtomic(project->filePath() / "project.json",
                                          R"({"version":1,"projectId":999999,"name":"Other"})"));

    EXPECT_EQ(m_manager->validateProjectStorage(project->id()),
              dw::ProjectStorageValidationStatus::IdentityMismatch);
}

TEST_F(ProjectStorageTest, StorageValidatorRejectsDuplicateDatabaseClaim) {
    auto owner = m_manager->create("Claim Owner");
    ASSERT_TRUE(owner);
    ASSERT_TRUE(m_manager->save(*owner));

    auto duplicate = m_manager->create("Duplicate Claim");
    ASSERT_TRUE(duplicate);
    duplicate->setFilePath(owner->filePath());
    dw::ProjectRepository projects(m_database);
    ASSERT_TRUE(projects.update(duplicate->record()));

    EXPECT_EQ(m_manager->validateProjectStorage(owner->id()),
              dw::ProjectStorageValidationStatus::DuplicateClaim);
}

TEST_F(ProjectStorageTest, StorageValidatorRejectsTemporaryProjectWithoutOwnershipMarker) {
    auto project = m_manager->create("Unowned Temporary", true);
    ASSERT_TRUE(project);
    ASSERT_TRUE(m_manager->save(*project));
    const dw::Path marker = project->filePath() / ".dw-temporary-project";
    ASSERT_TRUE(dw::file::remove(marker));

    EXPECT_EQ(m_manager->validateProjectStorage(project->id()),
              dw::ProjectStorageValidationStatus::InvalidTemporaryOwnership);
}

} // namespace
