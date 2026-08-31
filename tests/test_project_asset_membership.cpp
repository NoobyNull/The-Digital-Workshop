#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/project/project.h"
#include "core/project/project_asset_membership.h"
#include "core/project/project_directory.h"
#include "core/utils/file_utils.h"

namespace {

class ProjectAssetMembershipTest : public ::testing::Test {
  protected:
    void SetUp() override {
        static std::atomic<unsigned long long> sequence{0};
        m_root = std::filesystem::temp_directory_path() /
                 ("dw_project_asset_membership_" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::remove_all(m_root);
        ASSERT_TRUE(dw::file::createDirectories(m_root / "library"));

        m_oldProjectsDirectory = dw::Config::instance().getProjectsDir();
        dw::Config::instance().setProjectsDir(m_root / "Projects");
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_database));

        m_projects = std::make_unique<dw::ProjectManager>(m_database);
        m_project = m_projects->create("Membership Project");
        ASSERT_TRUE(m_project);
        ASSERT_TRUE(m_projects->save(*m_project));
        m_projects->synchronizeActiveProject(m_project);
        ASSERT_TRUE(m_projects->currentDirectory());
    }

    void TearDown() override {
        m_projects.reset();
        m_database.close();
        dw::Config::instance().setProjectsDir(m_oldProjectsDirectory);
        std::filesystem::remove_all(m_root);
    }

    dw::i64 addModel(const std::string& name,
                     const std::string& hash,
                     dw::Vec3 boundsMax = {0.0F, 0.0F, 0.0F}) {
        const auto path = m_root / "library" / (name + ".stl");
        EXPECT_TRUE(dw::file::writeText(path, "solid " + name + "\nendsolid\n"));
        dw::ModelRecord record;
        record.name = name;
        record.hash = hash;
        record.filePath = path;
        record.fileFormat = "stl";
        record.fileSize = dw::file::getFileSize(path).value_or(0);
        record.boundsMin = {0.0F, 0.0F, 0.0F};
        record.boundsMax = boundsMax;
        dw::ModelRepository repository(m_database);
        const auto id = repository.insert(record);
        EXPECT_TRUE(id.has_value());
        return id.value_or(0);
    }

    dw::i64 addMissingModel(const std::string& name, const std::string& hash) {
        dw::ModelRecord record;
        record.name = name;
        record.hash = hash;
        record.filePath = m_root / "library" / (name + "-missing.stl");
        record.fileFormat = "stl";
        dw::ModelRepository repository(m_database);
        const auto id = repository.insert(record);
        EXPECT_TRUE(id.has_value());
        return id.value_or(0);
    }

    dw::i64 addGCode(const std::string& name, const std::string& hash) {
        const auto path = m_root / "library" / (name + ".nc");
        EXPECT_TRUE(dw::file::writeText(path, "G90\nG0 X0 Y0\nM2\n"));
        dw::GCodeRecord record;
        record.name = name;
        record.hash = hash;
        record.filePath = path;
        record.fileSize = dw::file::getFileSize(path).value_or(0);
        dw::GCodeRepository repository(m_database);
        const auto id = repository.insert(record);
        EXPECT_TRUE(id.has_value());
        return id.value_or(0);
    }

    dw::i64 totalChanges() {
        auto statement = m_database.prepare("SELECT total_changes()");
        EXPECT_TRUE(statement.isValid());
        EXPECT_TRUE(statement.step());
        return statement.getInt(0);
    }

    dw::ProjectAssetMembershipResult ensure(
        std::vector<dw::ProjectAssetRef> assets,
        dw::i64 expectedProjectId = 0) {
        dw::ProjectAssetMembershipService service(*m_projects);
        return service.ensure({expectedProjectId > 0 ? expectedProjectId : m_project->id(),
                               std::move(assets)});
    }

    dw::Path m_root;
    dw::Path m_oldProjectsDirectory;
    dw::Database m_database;
    std::unique_ptr<dw::ProjectManager> m_projects;
    std::shared_ptr<dw::Project> m_project;
};

TEST_F(ProjectAssetMembershipTest, AddsMixedAssetsAndReportsDeduplicatedOutcomes) {
    const auto modelId = addModel("river-sign", "model-river-sign-hash");
    const auto gcodeId = addGCode("river-sign-rough", "gcode-river-sign-hash");
    const dw::ProjectAssetRef model{dw::ProjectAssetKind::Model, modelId};
    const dw::ProjectAssetRef gcode{dw::ProjectAssetKind::GCode, gcodeId};

    const auto result = ensure({model, gcode, model, gcode});

    ASSERT_EQ(result.status, dw::ProjectAssetMembershipStatus::Applied);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::None);
    ASSERT_EQ(result.items.size(), 4u);
    EXPECT_EQ(result.items[0].status, dw::ProjectAssetMembershipItemStatus::Added);
    EXPECT_EQ(result.items[1].status, dw::ProjectAssetMembershipItemStatus::Added);
    EXPECT_EQ(result.items[2].status,
              dw::ProjectAssetMembershipItemStatus::DuplicateRequest);
    EXPECT_EQ(result.items[3].status,
              dw::ProjectAssetMembershipItemStatus::DuplicateRequest);

    dw::ProjectRepository projects(m_database);
    dw::GCodeRepository gcodeRepository(m_database);
    EXPECT_TRUE(projects.hasModel(m_project->id(), modelId));
    EXPECT_TRUE(gcodeRepository.isInProject(m_project->id(), gcodeId));
    ASSERT_EQ(projects.findOpenItemsBySource(m_project->id(), "models", modelId).size(), 1u);
    ASSERT_EQ(projects.findOpenItemsBySource(m_project->id(), "gcode_files", gcodeId).size(), 1u);

    dw::ProjectDirectory directory;
    ASSERT_TRUE(directory.inspect(m_project->filePath()));
    EXPECT_EQ(directory.models().size(), 1u);
    EXPECT_EQ(directory.gcodeFiles().size(), 1u);
    EXPECT_FALSE(m_project->isModified());
}

TEST_F(ProjectAssetMembershipTest, RejectsTwoNewModelsWithoutWritingAnything) {
    const auto firstId = addModel("first-design", "first-design-hash");
    const auto secondId = addModel("second-design", "second-design-hash");
    const auto manifestBefore =
        dw::file::readText(m_project->filePath() / "project.json");
    const auto changesBefore = totalChanges();
    const auto directoryBefore = m_projects->currentDirectory();

    const auto result = ensure({{dw::ProjectAssetKind::Model, firstId},
                                {dw::ProjectAssetKind::Model, secondId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure,
              dw::ProjectAssetMembershipFailure::ModelLimitExceeded);
    ASSERT_EQ(result.items.size(), 2U);
    EXPECT_EQ(result.items[0].status,
              dw::ProjectAssetMembershipItemStatus::ModelLimitExceeded);
    EXPECT_EQ(result.items[1].status,
              dw::ProjectAssetMembershipItemStatus::ModelLimitExceeded);
    EXPECT_EQ(totalChanges(), changesBefore);
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"),
              manifestBefore);
    EXPECT_EQ(m_projects->currentDirectory(), directoryBefore);
    EXPECT_TRUE(m_project->modelIds().empty());
}

TEST_F(ProjectAssetMembershipTest, RejectsASecondModelWithoutReplacingTheFirst) {
    const auto firstId = addModel("chosen-design", "chosen-design-hash");
    const auto secondId = addModel("other-design", "other-design-hash");
    ASSERT_EQ(ensure({{dw::ProjectAssetKind::Model, firstId}}).status,
              dw::ProjectAssetMembershipStatus::Applied);
    const auto manifestBefore =
        dw::file::readText(m_project->filePath() / "project.json");
    const auto changesBefore = totalChanges();
    const auto directoryBefore = m_projects->currentDirectory();
    const auto modelIdsBefore = m_project->modelIds();

    const auto result = ensure({{dw::ProjectAssetKind::Model, secondId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure,
              dw::ProjectAssetMembershipFailure::ModelLimitExceeded);
    ASSERT_EQ(result.items.size(), 1U);
    EXPECT_EQ(result.items.front().status,
              dw::ProjectAssetMembershipItemStatus::ModelLimitExceeded);
    EXPECT_EQ(totalChanges(), changesBefore);
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"),
              manifestBefore);
    EXPECT_EQ(m_projects->currentDirectory(), directoryBefore);
    EXPECT_EQ(m_project->modelIds(), modelIdsBefore);
    dw::ProjectRepository projects(m_database);
    EXPECT_TRUE(projects.hasModel(m_project->id(), firstId));
    EXPECT_FALSE(projects.hasModel(m_project->id(), secondId));
}

TEST_F(ProjectAssetMembershipTest, LegacyMultipleModelsRemainWhileGCodeIsAdded) {
    const auto firstId = addModel("legacy-first", "legacy-first-hash");
    const auto secondId = addModel("legacy-second", "legacy-second-hash");
    ASSERT_EQ(ensure({{dw::ProjectAssetKind::Model, firstId}}).status,
              dw::ProjectAssetMembershipStatus::Applied);
    dw::ProjectRepository projects(m_database);
    ASSERT_TRUE(projects.addModel(m_project->id(), secondId));
    m_project->addModel(secondId);
    m_project->clearModified();
    const auto gcodeId = addGCode("legacy-program", "legacy-program-hash");

    const auto result = ensure({{dw::ProjectAssetKind::GCode, gcodeId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Applied);
    EXPECT_TRUE(projects.hasModel(m_project->id(), firstId));
    EXPECT_TRUE(projects.hasModel(m_project->id(), secondId));
    EXPECT_EQ(projects.getModelIds(m_project->id()).size(), 2U);
    EXPECT_EQ(m_project->modelIds().size(), 2U);
    dw::GCodeRepository gcodes(m_database);
    EXPECT_TRUE(gcodes.isInProject(m_project->id(), gcodeId));
    dw::ProjectDirectory directory;
    ASSERT_TRUE(directory.inspect(m_project->filePath()));
    EXPECT_EQ(directory.models().size(), 2U);
    EXPECT_EQ(directory.gcodeFiles().size(), 1U);
}

TEST_F(ProjectAssetMembershipTest,
       MembershipModelWithLivePrecisionBoundsRemainsReadyAfterValidation) {
    const dw::Vec3 liveBounds{139.10118103027344F, 250.0F, 16.91642189025879F};
    const auto modelId =
        addModel("live-precision-bounds", "live-precision-bounds-hash", liveBounds);

    ASSERT_EQ(ensure({{dw::ProjectAssetKind::Model, modelId}}).status,
              dw::ProjectAssetMembershipStatus::Applied);

    dw::ProjectRepository projects(m_database);
    const auto created =
        projects.findOpenItemsBySource(m_project->id(), "models", modelId);
    ASSERT_EQ(created.size(), 1U);
    EXPECT_EQ(created.front().status, dw::ProjectOpenItemStatus::Ready);
    const auto snapshot = nlohmann::json::parse(created.front().snapshotJson);
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][0].get<double>(),
                     static_cast<double>(liveBounds.x));
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][2].get<double>(),
                     static_cast<double>(liveBounds.z));

    EXPECT_EQ(projects.validateOpenItemsForProject(m_project->id()), 0);
    const auto validated = projects.findOpenItemById(created.front().id);
    ASSERT_TRUE(validated.has_value());
    EXPECT_EQ(validated->status, dw::ProjectOpenItemStatus::Ready);
}

TEST_F(ProjectAssetMembershipTest, AllExistingIsWriteFreeAndLeavesManifestBytesUnchanged) {
    const auto modelId = addModel("existing-model", "existing-model-hash");
    const auto gcodeId = addGCode("existing-gcode", "existing-gcode-hash");
    const dw::ProjectAssetRef model{dw::ProjectAssetKind::Model, modelId};
    const dw::ProjectAssetRef gcode{dw::ProjectAssetKind::GCode, gcodeId};
    ASSERT_EQ(ensure({model, gcode}).status, dw::ProjectAssetMembershipStatus::Applied);

    dw::ProjectRepository projects(m_database);
    const auto recordBefore = projects.findById(m_project->id());
    const auto manifestBefore = dw::file::readText(m_project->filePath() / "project.json");
    const auto changesBefore = totalChanges();
    const auto directoryBefore = m_projects->currentDirectory();
    ASSERT_TRUE(recordBefore);
    ASSERT_TRUE(manifestBefore);

    const auto result = ensure({model, gcode, model});

    ASSERT_EQ(result.status, dw::ProjectAssetMembershipStatus::Unchanged);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::None);
    ASSERT_EQ(result.items.size(), 3u);
    EXPECT_EQ(result.items[0].status,
              dw::ProjectAssetMembershipItemStatus::AlreadyMember);
    EXPECT_EQ(result.items[1].status,
              dw::ProjectAssetMembershipItemStatus::AlreadyMember);
    EXPECT_EQ(result.items[2].status,
              dw::ProjectAssetMembershipItemStatus::DuplicateRequest);
    EXPECT_EQ(totalChanges(), changesBefore);
    ASSERT_TRUE(projects.findById(m_project->id()));
    EXPECT_EQ(projects.findById(m_project->id())->modifiedAt, recordBefore->modifiedAt);
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"), manifestBefore);
    EXPECT_EQ(m_projects->currentDirectory(), directoryBefore);
}

TEST_F(ProjectAssetMembershipTest, RejectsHostileAssetKindBeforeAnyPersistence) {
    const auto modelId = addModel("hostile-kind", "hostile-kind-hash");
    const auto manifestBefore = dw::file::readText(m_project->filePath() / "project.json");
    const auto changesBefore = totalChanges();
    const dw::ProjectAssetRef hostile{static_cast<dw::ProjectAssetKind>(99), modelId};

    const auto result = ensure({hostile});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::InvalidAssetKind);
    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].status,
              dw::ProjectAssetMembershipItemStatus::InvalidAssetKind);
    EXPECT_EQ(totalChanges(), changesBefore);
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"), manifestBefore);
    dw::ProjectRepository projects(m_database);
    EXPECT_FALSE(projects.hasModel(m_project->id(), modelId));
}

TEST_F(ProjectAssetMembershipTest, PrevalidationReportsEveryBadSourceAndCommitsNothing) {
    const auto validModelId = addModel("valid-model", "valid-model-hash");
    const auto missingFileId = addMissingModel("missing-file", "missing-file-hash");
    const dw::ProjectAssetRef valid{dw::ProjectAssetKind::Model, validModelId};
    const dw::ProjectAssetRef missingRow{dw::ProjectAssetKind::GCode, 999999};
    const dw::ProjectAssetRef missingFile{dw::ProjectAssetKind::Model, missingFileId};
    const auto manifestBefore = dw::file::readText(m_project->filePath() / "project.json");
    const auto changesBefore = totalChanges();

    const auto result = ensure({valid, missingRow, missingFile});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::SourceMissing);
    ASSERT_EQ(result.items.size(), 3u);
    EXPECT_EQ(result.items[0].status,
              dw::ProjectAssetMembershipItemStatus::NotCommitted);
    EXPECT_EQ(result.items[1].status,
              dw::ProjectAssetMembershipItemStatus::SourceMissing);
    EXPECT_EQ(result.items[2].status,
              dw::ProjectAssetMembershipItemStatus::SourceFileMissing);
    EXPECT_EQ(totalChanges(), changesBefore);
    dw::ProjectRepository projects(m_database);
    EXPECT_FALSE(projects.hasModel(m_project->id(), validModelId));
    EXPECT_EQ(projects.findOpenItemsBySource(m_project->id(), "models", validModelId).size(),
              0u);
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"), manifestBefore);
}

TEST_F(ProjectAssetMembershipTest, RequiresTheExactExpectedActiveProject) {
    const auto modelId = addModel("wrong-project", "wrong-project-hash");
    const auto changesBefore = totalChanges();

    const auto result = ensure({{dw::ProjectAssetKind::Model, modelId}},
                               m_project->id() + 1000);

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::ProjectMismatch);
    EXPECT_EQ(totalChanges(), changesBefore);
    dw::ProjectRepository projects(m_database);
    EXPECT_FALSE(projects.hasModel(m_project->id(), modelId));
}

TEST_F(ProjectAssetMembershipTest, StoragePreflightDoesNotRepairOrWriteAnInvalidDirectory) {
    const auto modelId = addModel("storage-preflight", "storage-preflight-hash");
    const auto modelsDirectory = m_project->filePath() / "models";
    std::filesystem::remove_all(modelsDirectory);
    ASSERT_FALSE(dw::file::exists(modelsDirectory));
    const auto manifestBefore = dw::file::readText(m_project->filePath() / "project.json");
    const auto changesBefore = totalChanges();

    const auto result = ensure({{dw::ProjectAssetKind::Model, modelId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::StorageUnavailable);
    EXPECT_EQ(totalChanges(), changesBefore);
    EXPECT_FALSE(dw::file::exists(modelsDirectory));
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"), manifestBefore);
}

TEST_F(ProjectAssetMembershipTest, ExistingMembershipWithoutOpenItemFailsWithoutRepairWrites) {
    const auto modelId = addModel("missing-open-item", "missing-open-item-hash");
    dw::ProjectRepository projects(m_database);
    ASSERT_TRUE(projects.addModel(m_project->id(), modelId));
    ASSERT_TRUE(projects.findOpenItemsBySource(m_project->id(), "models", modelId).empty());
    const auto changesBefore = totalChanges();

    const auto result = ensure({{dw::ProjectAssetKind::Model, modelId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::OpenItemMissing);
    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].status,
              dw::ProjectAssetMembershipItemStatus::OpenItemMissing);
    EXPECT_EQ(totalChanges(), changesBefore);
    EXPECT_TRUE(projects.findOpenItemsBySource(m_project->id(), "models", modelId).empty());
}

TEST_F(ProjectAssetMembershipTest, ProjectsDatabaseTruthWithoutSavingOtherDirtyChanges) {
    const auto persistedId = addModel("persisted", "persisted-model-hash");
    ASSERT_EQ(ensure({{dw::ProjectAssetKind::Model, persistedId}}).status,
              dw::ProjectAssetMembershipStatus::Applied);

    const auto unsavedId = addModel("unsaved", "unsaved-model-hash");
    ASSERT_TRUE(m_projects->addModelToProject(unsavedId));
    ASSERT_TRUE(m_projects->removeModelFromProject(persistedId));
    m_project->setDescription("unsaved description");
    m_project->markModified();
    const auto gcodeId = addGCode("new-toolpath", "new-toolpath-hash");

    const auto result = ensure({{dw::ProjectAssetKind::GCode, gcodeId}});

    ASSERT_EQ(result.status, dw::ProjectAssetMembershipStatus::Applied);
    EXPECT_TRUE(m_project->isModified());
    EXPECT_FALSE(m_project->hasModel(persistedId));
    EXPECT_TRUE(m_project->hasModel(unsavedId));
    EXPECT_EQ(m_project->description(), "unsaved description");

    dw::ProjectRepository projects(m_database);
    EXPECT_TRUE(projects.hasModel(m_project->id(), persistedId));
    EXPECT_FALSE(projects.hasModel(m_project->id(), unsavedId));
    const auto record = projects.findById(m_project->id());
    ASSERT_TRUE(record);
    EXPECT_TRUE(record->description.empty());

    dw::ProjectDirectory directory;
    ASSERT_TRUE(directory.inspect(m_project->filePath()));
    ASSERT_EQ(directory.models().size(), 1u);
    EXPECT_EQ(directory.models().front().hash, "persisted-model-hash");
    EXPECT_EQ(directory.gcodeFiles().size(), 1u);
    EXPECT_TRUE(directory.description().empty());
}

TEST_F(ProjectAssetMembershipTest, FailsCleanlyWhenCheckedTransactionCannotStart) {
    const auto modelId = addModel("busy-transaction", "busy-transaction-hash");
    const auto manifestBefore = dw::file::readText(m_project->filePath() / "project.json");
    ASSERT_TRUE(m_database.beginTransaction());

    const auto result = ensure({{dw::ProjectAssetKind::Model, modelId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::TransactionUnavailable);
    ASSERT_TRUE(m_database.rollback());
    dw::ProjectRepository projects(m_database);
    EXPECT_FALSE(projects.hasModel(m_project->id(), modelId));
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"), manifestBefore);
}

TEST_F(ProjectAssetMembershipTest, CommitFailureRestoresDatabaseDirectoryAndMemory) {
    const auto modelId = addModel("rollback-model", "rollback-model-hash");
    const auto gcodeId = addGCode("rollback-gcode", "rollback-gcode-hash");
    const auto manifestBefore = dw::file::readText(m_project->filePath() / "project.json");
    const auto directoryBefore = m_projects->currentDirectory();
    const auto modelIdsBefore = m_project->modelIds();
    const bool dirtyBefore = m_project->isModified();

    ASSERT_TRUE(m_database.execute(R"(
        CREATE TABLE membership_commit_guard (
            id INTEGER PRIMARY KEY,
            project_id INTEGER,
            FOREIGN KEY(project_id) REFERENCES projects(id)
                DEFERRABLE INITIALLY DEFERRED
        )
    )"));
    ASSERT_TRUE(m_database.execute(
        "CREATE TRIGGER reject_membership_commit AFTER UPDATE OF modified_at ON projects "
        "WHEN NEW.id = " + std::to_string(m_project->id()) +
        " BEGIN INSERT INTO membership_commit_guard(project_id) VALUES(-999); END"));

    const auto result = ensure({{dw::ProjectAssetKind::Model, modelId},
                                {dw::ProjectAssetKind::GCode, gcodeId}});

    EXPECT_EQ(result.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(result.failure, dw::ProjectAssetMembershipFailure::TransactionCommitFailed);
    dw::ProjectRepository projects(m_database);
    dw::GCodeRepository gcode(m_database);
    EXPECT_FALSE(projects.hasModel(m_project->id(), modelId));
    EXPECT_FALSE(gcode.isInProject(m_project->id(), gcodeId));
    EXPECT_TRUE(projects.findOpenItemsBySource(m_project->id(), "models", modelId).empty());
    EXPECT_TRUE(projects.findOpenItemsBySource(m_project->id(), "gcode_files", gcodeId).empty());
    EXPECT_EQ(dw::file::readText(m_project->filePath() / "project.json"), manifestBefore);
    EXPECT_EQ(m_projects->currentDirectory(), directoryBefore);
    EXPECT_EQ(m_project->modelIds(), modelIdsBefore);
    EXPECT_EQ(m_project->isModified(), dirtyBefore);

    for (const auto& entry : std::filesystem::directory_iterator(m_project->filePath().parent_path())) {
        EXPECT_EQ(entry.path().filename().string().find(".dw-membership-"),
                  std::string::npos);
    }
}

} // namespace
