// Regression tests for repository-owned Project Plan hierarchy synchronization.

#include <gtest/gtest.h>

#include <memory>

#include <nlohmann/json.hpp>

#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"

namespace {

class ProjectOpenItemRepositorySyncTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_database));
        m_projects = std::make_unique<dw::ProjectRepository>(m_database);
    }

    dw::i64 insertProject(const std::string& name) {
        dw::ProjectRecord project;
        project.name = name;
        return m_projects->insert(project).value();
    }

    dw::i64 insertModel(const std::string& name,
                        dw::Vec3 boundsMax = {100.0F, 50.0F, 6.0F}) {
        dw::ModelRepository models(m_database);
        dw::ModelRecord model;
        model.name = name;
        model.hash = "model-hash";
        model.filePath = "/models/" + name + ".stl";
        model.fileFormat = "stl";
        model.boundsMin = {0.0F, 0.0F, 0.0F};
        model.boundsMax = boundsMax;
        return models.insert(model).value();
    }

    dw::GCodeRecord generatedGCode() const {
        dw::GCodeRecord gcode;
        gcode.name = "River Sign Ready";
        gcode.hash = "generated-hash";
        gcode.filePath = "/gcode/river-sign-ready.nc";
        gcode.fileSize = 512;
        gcode.estimatedTime = 42.5F;
        gcode.feedRates = {300.0F, 1000.0F};
        gcode.toolNumbers = {1, 3};
        return gcode;
    }

    dw::ProjectOpenItem generatedProgram(dw::i64 projectId,
                                         dw::i64 gcodeId,
                                         dw::i64 operationItemId,
                                         const dw::GCodeRecord& source) const {
        dw::ProjectOpenItem item;
        item.projectId = projectId;
        item.itemType = dw::ProjectOpenItemType::Gcode;
        item.sourceTable = "gcode_files";
        item.sourceId = gcodeId;
        item.sourceKey = "gcode_files:" + std::to_string(gcodeId);
        item.parentItemId = operationItemId;
        item.status = dw::ProjectOpenItemStatus::Ready;
        item.displayName = source.name;
        item.intentJson = R"({"role":"generated_direct_carve_program"})";
        item.snapshotJson = nlohmann::json{
            {"hash", source.hash},
            {"file_path", source.filePath.string()},
            {"file_size", source.fileSize},
            {"estimated_time", source.estimatedTime},
            {"feed_rates", source.feedRates},
            {"tool_numbers", source.toolNumbers},
        }.dump();
        return item;
    }

    dw::Database m_database;
    std::unique_ptr<dw::ProjectRepository> m_projects;
};

} // namespace

TEST_F(ProjectOpenItemRepositorySyncTest,
       ExplicitGeneratedDirectCarveProgramKeepsItsOperationIdentity) {
    const auto projectId = insertProject("River Sign");
    const auto modelId = insertModel("River Sign");
    ASSERT_TRUE(m_projects->addModel(projectId, modelId));
    ASSERT_EQ(m_projects->ensureOpenItemsForProject(projectId), 1);

    const auto modelItems = m_projects->findOpenItemsBySource(projectId, "models", modelId);
    ASSERT_EQ(modelItems.size(), 1U);

    dw::ProjectOpenItem operation;
    operation.projectId = projectId;
    operation.itemType = dw::ProjectOpenItemType::Operation;
    operation.sourceTable = "direct_carve";
    operation.sourceKey = "direct_carve:model_item:" + std::to_string(modelItems.front().id);
    operation.parentItemId = modelItems.front().id;
    operation.status = dw::ProjectOpenItemStatus::Ready;
    operation.displayName = "Direct Carve: River Sign";
    operation.intentJson = R"({"operation_kind":"direct_carve"})";
    const auto operationItemId = m_projects->insertOpenItem(operation);
    ASSERT_TRUE(operationItemId.has_value());

    dw::GCodeRepository gcodes(m_database);
    const auto gcode = generatedGCode();
    const auto gcodeId = gcodes.insert(gcode);
    ASSERT_TRUE(gcodeId.has_value());
    ASSERT_TRUE(gcodes.addToProject(projectId, *gcodeId));
    const auto programItemId = m_projects->insertOpenItem(
        generatedProgram(projectId, *gcodeId, *operationItemId, gcode));
    ASSERT_TRUE(programItemId.has_value());

    const auto groupId = gcodes.createGroup(modelId, "Direct Carve", 0);
    ASSERT_TRUE(groupId.has_value());
    ASSERT_TRUE(gcodes.addToGroup(*groupId, *gcodeId));

    EXPECT_EQ(m_projects->ensureOpenItemsForProject(projectId), 2);
    EXPECT_TRUE(
        m_projects->findOpenItemsBySource(projectId, "operation_groups", *groupId).empty());

    const auto programs =
        m_projects->findOpenItemsBySource(projectId, "gcode_files", *gcodeId);
    ASSERT_EQ(programs.size(), 1U);
    EXPECT_EQ(programs.front().id, *programItemId);
    EXPECT_EQ(programs.front().parentItemId, operationItemId);
    EXPECT_EQ(programs.front().intentJson,
              R"({"role":"generated_direct_carve_program"})");

    for (const int toolNumber : gcode.toolNumbers) {
        const auto tools = m_projects->findOpenItemsBySourceKey(
            projectId,
            "gcode_files:" + std::to_string(*gcodeId) + ":tool:" +
                std::to_string(toolNumber));
        ASSERT_EQ(tools.size(), 1U);
        EXPECT_EQ(tools.front().parentItemId, programItemId);
    }

    EXPECT_EQ(m_projects->validateOpenItemsForProject(projectId), 0);
    EXPECT_EQ(m_projects->ensureOpenItemsForProject(projectId), 0);
    EXPECT_EQ(m_projects->findOpenItemsBySource(projectId, "gcode_files", *gcodeId)
                  .front()
                  .parentItemId,
              operationItemId);
}

TEST_F(ProjectOpenItemRepositorySyncTest,
       SynchronizedModelWithLivePrecisionBoundsRemainsReadyAfterValidation) {
    const auto projectId = insertProject("Live Precision Model");
    const dw::Vec3 liveBounds{166.45442199707031F, 250.0F, 16.37860107421875F};
    const auto modelId = insertModel("Pig Tray", liveBounds);
    ASSERT_TRUE(m_projects->addModel(projectId, modelId));
    ASSERT_EQ(m_projects->ensureOpenItemsForProject(projectId), 1);

    const auto created =
        m_projects->findOpenItemsBySource(projectId, "models", modelId);
    ASSERT_EQ(created.size(), 1U);
    EXPECT_EQ(created.front().status, dw::ProjectOpenItemStatus::Ready);
    const auto snapshot = nlohmann::json::parse(created.front().snapshotJson);
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][0].get<double>(),
                     static_cast<double>(liveBounds.x));
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][2].get<double>(),
                     static_cast<double>(liveBounds.z));

    EXPECT_EQ(m_projects->validateOpenItemsForProject(projectId), 0);
    const auto validated = m_projects->findOpenItemById(created.front().id);
    ASSERT_TRUE(validated.has_value());
    EXPECT_EQ(validated->status, dw::ProjectOpenItemStatus::Ready);
}

TEST_F(ProjectOpenItemRepositorySyncTest,
       LegacyRoundedStaleModelSnapshotIsRevalidatedAndRewrittenAtFullPrecision) {
    const auto projectId = insertProject("Legacy Rounded Model");
    const dw::Vec3 liveBounds{166.45442199707031F, 250.0F, 16.37860107421875F};
    const auto modelId = insertModel("Legacy Pig Tray", liveBounds);
    ASSERT_TRUE(m_projects->addModel(projectId, modelId));

    dw::ProjectOpenItem legacy;
    legacy.projectId = projectId;
    legacy.itemType = dw::ProjectOpenItemType::Model;
    legacy.sourceTable = "models";
    legacy.sourceId = modelId;
    legacy.status = dw::ProjectOpenItemStatus::Stale;
    legacy.displayName = "Legacy Pig Tray";
    legacy.intentJson = R"({"role":"project_model"})";
    legacy.snapshotJson =
        R"({"hash":"model-hash","file_path":"/models/Legacy Pig Tray.stl","file_format":"stl","bounds":{"min":[0,0,0],"max":[166.454,250,16.3786]}})";
    const auto itemId = m_projects->insertOpenItem(legacy);
    ASSERT_TRUE(itemId.has_value());

    EXPECT_EQ(m_projects->validateOpenItemsForProject(projectId), 1);
    const auto repaired = m_projects->findOpenItemById(*itemId);
    ASSERT_TRUE(repaired.has_value());
    EXPECT_EQ(repaired->status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_NE(repaired->snapshotJson, legacy.snapshotJson);

    const auto snapshot = nlohmann::json::parse(repaired->snapshotJson);
    ASSERT_TRUE(snapshot.contains("bounds"));
    ASSERT_TRUE(snapshot["bounds"].contains("max"));
    ASSERT_EQ(snapshot["bounds"]["max"].size(), 3U);
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][0].get<double>(),
                     static_cast<double>(liveBounds.x));
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][1].get<double>(),
                     static_cast<double>(liveBounds.y));
    EXPECT_DOUBLE_EQ(snapshot["bounds"]["max"][2].get<double>(),
                     static_cast<double>(liveBounds.z));
    EXPECT_EQ(m_projects->validateOpenItemsForProject(projectId), 0);
}

TEST_F(ProjectOpenItemRepositorySyncTest,
       GCodeJsonArraysCompareByValueAndStillDetectRealChanges) {
    const auto projectId = insertProject("Semantic G-code");
    dw::GCodeRepository gcodes(m_database);
    const auto gcode = generatedGCode();
    const auto gcodeId = gcodes.insert(gcode);
    ASSERT_TRUE(gcodeId.has_value());

    auto program = generatedProgram(projectId, *gcodeId, 0, gcode);
    program.parentItemId.reset();
    auto snapshot = nlohmann::json::parse(program.snapshotJson);
    snapshot["feed_rates"] = nlohmann::json::array({300.0, 1000.0});
    snapshot["tool_numbers"] = nlohmann::json::array({1.0, 3.0});
    program.snapshotJson = snapshot.dump();
    const auto programId = m_projects->insertOpenItem(program);
    ASSERT_TRUE(programId.has_value());

    EXPECT_EQ(m_projects->validateOpenItemsForProject(projectId), 0);
    EXPECT_EQ(m_projects->findOpenItemById(*programId)->status,
              dw::ProjectOpenItemStatus::Ready);

    ASSERT_TRUE(m_database.execute(
        "UPDATE gcode_files SET feed_rates = '[300,1200]' WHERE id = " +
        std::to_string(*gcodeId)));
    EXPECT_EQ(m_projects->validateOpenItemsForProject(projectId), 1);
    EXPECT_EQ(m_projects->findOpenItemById(*programId)->status,
              dw::ProjectOpenItemStatus::Stale);
}

TEST_F(ProjectOpenItemRepositorySyncTest,
       MixedGroupKeepsOrdinaryBranchWithoutReparentingExplicitProgram) {
    const auto projectId = insertProject("Mixed Programs");
    const auto modelId = insertModel("Relief");
    ASSERT_TRUE(m_projects->addModel(projectId, modelId));
    ASSERT_EQ(m_projects->ensureOpenItemsForProject(projectId), 1);
    const auto modelItem =
        m_projects->findOpenItemsBySource(projectId, "models", modelId).front();

    dw::ProjectOpenItem operation;
    operation.projectId = projectId;
    operation.itemType = dw::ProjectOpenItemType::Operation;
    operation.sourceTable = "direct_carve";
    operation.sourceKey = "direct_carve:model_item:" + std::to_string(modelItem.id);
    operation.parentItemId = modelItem.id;
    operation.status = dw::ProjectOpenItemStatus::Ready;
    operation.displayName = "Direct Carve: Relief";
    operation.intentJson = R"({"operation_kind":"direct_carve"})";
    const auto operationItemId = m_projects->insertOpenItem(operation);
    ASSERT_TRUE(operationItemId.has_value());

    dw::GCodeRepository gcodes(m_database);
    const auto explicitGcode = generatedGCode();
    const auto explicitGcodeId = gcodes.insert(explicitGcode);
    ASSERT_TRUE(explicitGcodeId.has_value());
    ASSERT_TRUE(gcodes.addToProject(projectId, *explicitGcodeId));
    const auto explicitProgramId = m_projects->insertOpenItem(
        generatedProgram(projectId, *explicitGcodeId, *operationItemId, explicitGcode));
    ASSERT_TRUE(explicitProgramId.has_value());

    auto ordinaryGcode = generatedGCode();
    ordinaryGcode.name = "Imported Finish";
    ordinaryGcode.hash = "imported-hash";
    ordinaryGcode.filePath = "/gcode/imported-finish.nc";
    const auto ordinaryGcodeId = gcodes.insert(ordinaryGcode);
    ASSERT_TRUE(ordinaryGcodeId.has_value());
    ASSERT_TRUE(gcodes.addToProject(projectId, *ordinaryGcodeId));

    const auto groupId = gcodes.createGroup(modelId, "Mixed Toolpaths", 0);
    ASSERT_TRUE(groupId.has_value());
    ASSERT_TRUE(gcodes.addToGroup(*groupId, *explicitGcodeId));
    ASSERT_TRUE(gcodes.addToGroup(*groupId, *ordinaryGcodeId));

    EXPECT_GT(m_projects->ensureOpenItemsForProject(projectId), 0);
    const auto groupItems =
        m_projects->findOpenItemsBySource(projectId, "operation_groups", *groupId);
    ASSERT_EQ(groupItems.size(), 1U);

    const auto explicitPrograms =
        m_projects->findOpenItemsBySource(projectId, "gcode_files", *explicitGcodeId);
    ASSERT_EQ(explicitPrograms.size(), 1U);
    EXPECT_EQ(explicitPrograms.front().parentItemId, operationItemId);

    const auto ordinaryPrograms =
        m_projects->findOpenItemsBySource(projectId, "gcode_files", *ordinaryGcodeId);
    ASSERT_EQ(ordinaryPrograms.size(), 1U);
    EXPECT_EQ(ordinaryPrograms.front().parentItemId, groupItems.front().id);

    const auto explicitTool = m_projects->findOpenItemsBySourceKey(
        projectId, "gcode_files:" + std::to_string(*explicitGcodeId) + ":tool:1");
    ASSERT_EQ(explicitTool.size(), 1U);
    EXPECT_EQ(explicitTool.front().parentItemId, explicitProgramId);

    const auto ordinaryTool = m_projects->findOpenItemsBySourceKey(
        projectId, "gcode_files:" + std::to_string(*ordinaryGcodeId) + ":tool:1");
    ASSERT_EQ(ordinaryTool.size(), 1U);
    EXPECT_EQ(ordinaryTool.front().parentItemId, groupItems.front().id);
}
