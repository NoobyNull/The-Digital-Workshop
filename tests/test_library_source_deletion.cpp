#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/library/library_manager.h"
#include "core/library/library_source_deletion.h"

namespace {

using dw::LibrarySourceDeletionItemStatus;
using dw::LibrarySourceDeletionStatus;
using dw::LibrarySourceKind;
using dw::LibrarySourceRef;

class LibrarySourceDeletionTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
        m_library = std::make_unique<dw::LibraryManager>(m_db);
        m_models = std::make_unique<dw::ModelRepository>(m_db);
        m_projects = std::make_unique<dw::ProjectRepository>(m_db);
        m_gcodes = std::make_unique<dw::GCodeRepository>(m_db);
        m_service =
            std::make_unique<dw::LibrarySourceDeletionService>(*m_library, *m_projects, *m_gcodes);
    }

    dw::i64 addModel(const std::string& name) {
        dw::ModelRecord model;
        model.hash = "model-hash-" + std::to_string(++m_nextHash);
        model.name = name;
        model.filePath = "/models/" + name + ".stl";
        model.fileFormat = "stl";
        return m_models->insert(model).value_or(0);
    }

    dw::i64 addGCode(const std::string& name) {
        dw::GCodeRecord gcode;
        gcode.hash = "gcode-hash-" + std::to_string(++m_nextHash);
        gcode.name = name;
        gcode.filePath = "/gcode/" + name + ".nc";
        return m_gcodes->insert(gcode).value_or(0);
    }

    dw::i64 addProject(const std::string& name) {
        dw::ProjectRecord project;
        project.name = name;
        project.filePath = "/projects/" + name;
        return m_projects->insert(project).value_or(0);
    }

    bool modelExists(dw::i64 id) { return m_library->getModel(id).has_value(); }
    bool gcodeExists(dw::i64 id) { return m_library->getGCodeFile(id).has_value(); }

    dw::Database m_db;
    std::unique_ptr<dw::LibraryManager> m_library;
    std::unique_ptr<dw::ModelRepository> m_models;
    std::unique_ptr<dw::ProjectRepository> m_projects;
    std::unique_ptr<dw::GCodeRepository> m_gcodes;
    std::unique_ptr<dw::LibrarySourceDeletionService> m_service;
    int m_nextHash = 0;
};

TEST_F(LibrarySourceDeletionTest, EmptyRequestIsANoOp) {
    const auto result = m_service->deleteSources({});

    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::EmptyRequest);
    EXPECT_TRUE(result.items.empty());
    EXPECT_TRUE(result.affectedProjects.empty());
}

TEST_F(LibrarySourceDeletionTest, RejectsInvalidAndMissingSources) {
    const std::vector<LibrarySourceRef> sources = {
        {LibrarySourceKind::Model, 0},
        {static_cast<LibrarySourceKind>(99), 4},
        {LibrarySourceKind::Model, 9998},
        {LibrarySourceKind::GCode, 9999},
    };

    const auto result = m_service->deleteSources(sources);

    ASSERT_EQ(result.items.size(), 4u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::PreflightRejected);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::InvalidSource);
    EXPECT_EQ(result.items[1].status, LibrarySourceDeletionItemStatus::InvalidSource);
    EXPECT_EQ(result.items[2].status, LibrarySourceDeletionItemStatus::MissingSource);
    EXPECT_EQ(result.items[3].status, LibrarySourceDeletionItemStatus::MissingSource);
}

TEST_F(LibrarySourceDeletionTest, ActivePreviewBlocksTheWholeBatch) {
    const dw::i64 modelId = addModel("previewed");
    const dw::i64 gcodeId = addGCode("otherwise-safe");
    const LibrarySourceRef preview{LibrarySourceKind::Model, modelId};

    const auto result = m_service->deleteSources({preview, {LibrarySourceKind::GCode, gcodeId}},
                                                 preview);

    ASSERT_EQ(result.items.size(), 2u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::PreflightRejected);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::ActivePreview);
    EXPECT_EQ(result.items[1].status, LibrarySourceDeletionItemStatus::BatchBlocked);
    EXPECT_FALSE(result.items[0].selectionCanClear());
    EXPECT_TRUE(modelExists(modelId));
    EXPECT_TRUE(gcodeExists(gcodeId));
}

TEST_F(LibrarySourceDeletionTest, CurrentAndClosedProjectLinksAreNamedAndBlocked) {
    const dw::i64 currentProject = addProject("Current Sign");
    const dw::i64 closedProject = addProject("Closed Clock");
    const dw::i64 modelId = addModel("shared-model");
    const dw::i64 gcodeId = addGCode("closed-project-cut");
    ASSERT_TRUE(m_projects->addModel(currentProject, modelId));
    ASSERT_TRUE(m_projects->addModel(closedProject, modelId));
    ASSERT_TRUE(m_gcodes->addToProject(closedProject, gcodeId));

    const auto result = m_service->deleteSources(
        {{LibrarySourceKind::Model, modelId}, {LibrarySourceKind::GCode, gcodeId}});

    ASSERT_EQ(result.items.size(), 2u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::PreflightRejected);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::LinkedToProjects);
    EXPECT_EQ(result.items[1].status, LibrarySourceDeletionItemStatus::LinkedToProjects);
    ASSERT_EQ(result.items[0].affectedProjects.size(), 2u);
    EXPECT_EQ(result.items[0].affectedProjects[0].id, currentProject);
    EXPECT_EQ(result.items[0].affectedProjects[0].name, "Current Sign");
    EXPECT_EQ(result.items[0].affectedProjects[1].id, closedProject);
    EXPECT_EQ(result.items[0].affectedProjects[1].name, "Closed Clock");
    ASSERT_EQ(result.affectedProjects.size(), 2u);
    EXPECT_TRUE(modelExists(modelId));
    EXPECT_TRUE(gcodeExists(gcodeId));
}

TEST_F(LibrarySourceDeletionTest, MixedBlockedBatchDoesNotDeleteSafeSource) {
    const dw::i64 safeModel = addModel("safe-but-batched");
    const dw::i64 linkedGCode = addGCode("linked");
    const dw::i64 projectId = addProject("Uses GCode");
    ASSERT_TRUE(m_gcodes->addToProject(projectId, linkedGCode));

    const auto result = m_service->deleteSources(
        {{LibrarySourceKind::Model, safeModel}, {LibrarySourceKind::GCode, linkedGCode}});

    ASSERT_EQ(result.items.size(), 2u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::PreflightRejected);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::BatchBlocked);
    EXPECT_EQ(result.items[1].status, LibrarySourceDeletionItemStatus::LinkedToProjects);
    EXPECT_TRUE(modelExists(safeModel));
    EXPECT_TRUE(gcodeExists(linkedGCode));
}

TEST_F(LibrarySourceDeletionTest, DeletesUnlinkedModelAndGCode) {
    const dw::i64 modelId = addModel("unlinked-model");
    const dw::i64 gcodeId = addGCode("unlinked-gcode");

    const auto result = m_service->deleteSources(
        {{LibrarySourceKind::Model, modelId}, {LibrarySourceKind::GCode, gcodeId}});

    ASSERT_EQ(result.items.size(), 2u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::Deleted);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::Deleted);
    EXPECT_EQ(result.items[1].status, LibrarySourceDeletionItemStatus::Deleted);
    EXPECT_TRUE(result.items[0].selectionCanClear());
    EXPECT_TRUE(result.items[1].selectionCanClear());
    EXPECT_FALSE(modelExists(modelId));
    EXPECT_FALSE(gcodeExists(gcodeId));
}

TEST_F(LibrarySourceDeletionTest, DuplicateSourcesAreDeletedOnceAndShareTruthfulOutcome) {
    const dw::i64 modelId = addModel("duplicate-model");
    const dw::i64 gcodeId = addGCode("duplicate-gcode");
    const LibrarySourceRef model{LibrarySourceKind::Model, modelId};
    const LibrarySourceRef gcode{LibrarySourceKind::GCode, gcodeId};

    const auto result = m_service->deleteSources({model, model, gcode, gcode});

    ASSERT_EQ(result.items.size(), 4u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::Deleted);
    EXPECT_FALSE(result.items[0].duplicate);
    EXPECT_TRUE(result.items[1].duplicate);
    EXPECT_FALSE(result.items[2].duplicate);
    EXPECT_TRUE(result.items[3].duplicate);
    for (const auto& item : result.items) {
        EXPECT_EQ(item.status, LibrarySourceDeletionItemStatus::Deleted);
        EXPECT_TRUE(item.selectionCanClear());
    }
    EXPECT_FALSE(modelExists(modelId));
    EXPECT_FALSE(gcodeExists(gcodeId));
}

TEST_F(LibrarySourceDeletionTest, FalseDeleteResultDoesNotClaimSuccessOrClearSelection) {
    const dw::i64 modelId = addModel("undeletable");
    ASSERT_TRUE(m_db.execute("CREATE TRIGGER reject_model_delete BEFORE DELETE ON models "
                             "BEGIN SELECT RAISE(FAIL, 'delete rejected'); END"));

    const auto result = m_service->deleteSources({{LibrarySourceKind::Model, modelId}});

    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::DeletionFailed);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::DeleteFailed);
    EXPECT_FALSE(result.items[0].selectionCanClear());
    EXPECT_TRUE(modelExists(modelId));
}

TEST_F(LibrarySourceDeletionTest, PartialDeleteReportsEachSourcesSelectionTruth) {
    const dw::i64 modelId = addModel("deletable");
    const dw::i64 gcodeId = addGCode("undeletable");
    ASSERT_TRUE(m_db.execute("CREATE TRIGGER reject_gcode_delete BEFORE DELETE ON gcode_files "
                             "BEGIN SELECT RAISE(FAIL, 'delete rejected'); END"));

    const auto result = m_service->deleteSources(
        {{LibrarySourceKind::Model, modelId}, {LibrarySourceKind::GCode, gcodeId}});

    ASSERT_EQ(result.items.size(), 2u);
    EXPECT_EQ(result.status, LibrarySourceDeletionStatus::PartiallyDeleted);
    EXPECT_EQ(result.items[0].status, LibrarySourceDeletionItemStatus::Deleted);
    EXPECT_EQ(result.items[1].status, LibrarySourceDeletionItemStatus::DeleteFailed);
    EXPECT_TRUE(result.items[0].selectionCanClear());
    EXPECT_FALSE(result.items[1].selectionCanClear());
    EXPECT_FALSE(modelExists(modelId));
    EXPECT_TRUE(gcodeExists(gcodeId));
}

} // namespace
