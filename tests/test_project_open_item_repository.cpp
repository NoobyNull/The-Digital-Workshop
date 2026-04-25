// Digital Workshop - Project Open Item Repository Tests

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/cost_repository.h"
#include "core/database/cut_plan_repository.h"
#include "core/database/gcode_repository.h"
#include "core/database/job_repository.h"
#include "core/database/material_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/database/stock_size_repository.h"

namespace {

class ProjectOpenItemRepoTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
        m_repo = std::make_unique<dw::ProjectRepository>(m_db);
    }

    dw::i64 insertProject(const std::string& name) {
        dw::ProjectRecord project;
        project.name = name;
        return m_repo->insert(project).value();
    }

    dw::ProjectOpenItem makeItem(dw::i64 projectId, const std::string& displayName) {
        dw::ProjectOpenItem item;
        item.projectId = projectId;
        item.itemType = dw::ProjectOpenItemType::Model;
        item.sourceTable = "models";
        item.sourceId = 42;
        item.status = dw::ProjectOpenItemStatus::Ready;
        item.displayName = displayName;
        item.intentJson = R"({"role":"carve_source","quantity":1})";
        item.snapshotJson = R"({"hash":"abc123","bounds":{"x":10}})";
        return item;
    }

    dw::i64 insertModel(const std::string& name, const std::string& hash) {
        dw::ModelRepository modelRepo(m_db);
        dw::ModelRecord model;
        model.name = name;
        model.hash = hash;
        model.filePath = "/models/" + name + ".stl";
        model.fileFormat = "stl";
        model.boundsMax = {10.0f, 20.0f, 3.0f};
        return modelRepo.insert(model).value();
    }

    dw::i64 insertGCode(const std::string& name, const std::string& hash) {
        dw::GCodeRepository gcodeRepo(m_db);
        dw::GCodeRecord gcode;
        gcode.name = name;
        gcode.hash = hash;
        gcode.filePath = "/gcode/" + name + ".nc";
        gcode.fileSize = 128;
        gcode.estimatedTime = 45.0f;
        gcode.toolNumbers = {1, 3};
        return gcodeRepo.insert(gcode).value();
    }

    dw::i64 insertJobForGCode(const std::string& name, const std::string& status) {
        dw::JobRepository jobRepo(m_db);
        dw::JobRecord job;
        job.fileName = name + ".nc";
        job.filePath = "/gcode/" + name + ".nc";
        job.totalLines = 100;
        job.lastAckedLine = status == "completed" ? 100 : 42;
        job.status = status;
        job.errorCount = 1;
        job.elapsedSeconds = 73.5f;
        return jobRepo.insert(job).value();
    }

    dw::i64 insertMaterial(const std::string& name) {
        dw::MaterialRepository materialRepo(m_db);
        dw::MaterialRecord material;
        material.name = name;
        material.category = dw::MaterialCategory::Hardwood;
        material.jankaHardness = 1010.0f;
        material.grainDirectionDeg = 90.0f;
        return materialRepo.insert(material).value();
    }

    dw::i64 insertStockSize(dw::i64 materialId, const std::string& name) {
        dw::StockSizeRepository stockRepo(m_db);
        dw::StockSize stock;
        stock.materialId = materialId;
        stock.name = name;
        stock.widthMm = 203.2;
        stock.heightMm = 609.6;
        stock.thicknessMm = 19.05;
        stock.pricePerUnit = 24.5;
        stock.unitLabel = "each";
        return stockRepo.insert(stock).value();
    }

    dw::Database m_db;
    std::unique_ptr<dw::ProjectRepository> m_repo;
};

} // namespace

TEST_F(ProjectOpenItemRepoTest, InsertOpenItem_PersistsIntentAndSnapshot) {
    auto projectId = insertProject("Open Item Project");

    auto id = m_repo->insertOpenItem(makeItem(projectId, "Relief model"));
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findOpenItemById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->projectId, projectId);
    EXPECT_EQ(found->itemType, dw::ProjectOpenItemType::Model);
    EXPECT_EQ(found->sourceTable, "models");
    EXPECT_EQ(found->sourceId, 42);
    EXPECT_EQ(found->sourceKey, "");
    EXPECT_EQ(found->status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(found->displayName, "Relief model");
    EXPECT_EQ(found->intentJson, R"({"role":"carve_source","quantity":1})");
    EXPECT_EQ(found->snapshotJson, R"({"hash":"abc123","bounds":{"x":10}})");
}

TEST_F(ProjectOpenItemRepoTest, ListOpenItemsForProject_OrdersParentsBeforeChildren) {
    auto projectId = insertProject("Tree Project");

    auto parent = makeItem(projectId, "Model");
    auto parentId = m_repo->insertOpenItem(parent).value();

    auto child = makeItem(projectId, "Direct Carve operation");
    child.itemType = dw::ProjectOpenItemType::Operation;
    child.parentItemId = parentId;
    child.sourceTable.clear();
    child.sourceId.reset();
    child.intentJson = R"({"kind":"direct_carve"})";
    auto childId = m_repo->insertOpenItem(child).value();

    auto items = m_repo->listOpenItemsForProject(projectId);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].id, parentId);
    EXPECT_EQ(items[1].id, childId);
    EXPECT_EQ(items[1].parentItemId, parentId);
}

TEST_F(ProjectOpenItemRepoTest, FindOpenItemsBySource_ReturnsMatchingLinksOnly) {
    auto firstProjectId = insertProject("First");
    auto secondProjectId = insertProject("Second");

    auto first = makeItem(firstProjectId, "Shared source");
    first.sourceId = 7;
    auto firstId = m_repo->insertOpenItem(first).value();

    auto second = makeItem(secondProjectId, "Same source elsewhere");
    second.sourceId = 7;
    m_repo->insertOpenItem(second);

    auto other = makeItem(firstProjectId, "Different source");
    other.sourceId = 8;
    m_repo->insertOpenItem(other);

    auto matches = m_repo->findOpenItemsBySource(firstProjectId, "models", 7);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].id, firstId);
    EXPECT_EQ(matches[0].displayName, "Shared source");
}

TEST_F(ProjectOpenItemRepoTest, FindOpenItemsBySourceKey_ReturnsExternalResourceLinks) {
    auto projectId = insertProject("Tools");

    dw::ProjectOpenItem item;
    item.projectId = projectId;
    item.itemType = dw::ProjectOpenItemType::Tool;
    item.sourceKey = "toolbox:tapered-ballnose-125";
    item.status = dw::ProjectOpenItemStatus::Planned;
    item.displayName = "1/8 tapered ball nose";

    auto id = m_repo->insertOpenItem(item).value();

    auto matches =
        m_repo->findOpenItemsBySourceKey(projectId, "toolbox:tapered-ballnose-125");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].id, id);
    EXPECT_EQ(matches[0].itemType, dw::ProjectOpenItemType::Tool);
}

TEST_F(ProjectOpenItemRepoTest, UpdateOpenItem_CanChangeStatusAndPayloads) {
    auto projectId = insertProject("Update");
    auto id = m_repo->insertOpenItem(makeItem(projectId, "Before")).value();

    auto item = m_repo->findOpenItemById(id).value();
    item.status = dw::ProjectOpenItemStatus::Stale;
    item.displayName = "After";
    item.intentJson = R"({"role":"cut_part","quantity":2})";
    item.snapshotJson = R"({"hash":"changed"})";

    EXPECT_TRUE(m_repo->updateOpenItem(item));

    auto updated = m_repo->findOpenItemById(id).value();
    EXPECT_EQ(updated.status, dw::ProjectOpenItemStatus::Stale);
    EXPECT_EQ(updated.displayName, "After");
    EXPECT_EQ(updated.intentJson, R"({"role":"cut_part","quantity":2})");
    EXPECT_EQ(updated.snapshotJson, R"({"hash":"changed"})");
}

TEST_F(ProjectOpenItemRepoTest, UpsertOpenItemBySourceKey_CreatesAndUpdatesStableOperation) {
    auto projectId = insertProject("Direct Carve Setup");

    dw::ProjectOpenItem item;
    item.projectId = projectId;
    item.itemType = dw::ProjectOpenItemType::Operation;
    item.sourceKey = "direct_carve:relief";
    item.status = dw::ProjectOpenItemStatus::Planned;
    item.displayName = "Direct Carve";
    item.intentJson = R"({"operation_kind":"direct_carve","safe_z":6})";
    item.snapshotJson = R"({"feed_rate":1200})";

    auto firstId = m_repo->upsertOpenItemBySourceKey(item);
    ASSERT_TRUE(firstId.has_value());

    item.status = dw::ProjectOpenItemStatus::Ready;
    item.displayName = "Direct Carve Finishing";
    item.intentJson = R"({"operation_kind":"direct_carve","safe_z":8})";
    item.snapshotJson = R"({"feed_rate":1500})";

    auto secondId = m_repo->upsertOpenItemBySourceKey(item);
    ASSERT_TRUE(secondId.has_value());
    EXPECT_EQ(*secondId, *firstId);

    auto child = makeItem(projectId, "Finishing Tool");
    child.itemType = dw::ProjectOpenItemType::Tool;
    child.sourceKey = "direct_carve:relief:tool:finish";
    child.parentItemId = *firstId;
    auto childId = m_repo->upsertOpenItemBySourceKey(child);
    ASSERT_TRUE(childId.has_value());

    auto matches = m_repo->findOpenItemsBySourceKey(projectId, "direct_carve:relief");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(matches[0].displayName, "Direct Carve Finishing");
    EXPECT_EQ(matches[0].intentJson, R"({"operation_kind":"direct_carve","safe_z":8})");
    EXPECT_EQ(matches[0].snapshotJson, R"({"feed_rate":1500})");

    auto toolMatches =
        m_repo->findOpenItemsBySourceKey(projectId, "direct_carve:relief:tool:finish");
    ASSERT_EQ(toolMatches.size(), 1u);
    EXPECT_EQ(toolMatches[0].parentItemId, *firstId);
}

TEST_F(ProjectOpenItemRepoTest, UpsertOpenItemBySource_CreatesAndUpdatesLinkedGCodeParent) {
    auto projectId = insertProject("Generated GCode");
    auto parentId = m_repo->insertOpenItem(makeItem(projectId, "Direct Carve")).value();
    auto gcodeId = insertGCode("Relief Finish", "gcode-hash");

    dw::ProjectOpenItem item;
    item.projectId = projectId;
    item.itemType = dw::ProjectOpenItemType::Gcode;
    item.sourceTable = "gcode_files";
    item.sourceId = gcodeId;
    item.status = dw::ProjectOpenItemStatus::Ready;
    item.displayName = "Relief Finish";
    item.intentJson = R"({"role":"generated_program"})";

    auto firstId = m_repo->upsertOpenItemBySource(item);
    ASSERT_TRUE(firstId.has_value());

    item.parentItemId = parentId;
    item.snapshotJson = R"({"estimated_time":12.5})";
    auto secondId = m_repo->upsertOpenItemBySource(item);
    ASSERT_TRUE(secondId.has_value());
    EXPECT_EQ(*firstId, *secondId);

    auto matches = m_repo->findOpenItemsBySource(projectId, "gcode_files", gcodeId);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].parentItemId, parentId);
    EXPECT_EQ(matches[0].snapshotJson, R"({"estimated_time":12.5})");
}

TEST_F(ProjectOpenItemRepoTest, RemoveOpenItem_RemovesChildrenByCascade) {
    auto projectId = insertProject("Delete");
    auto parentId = m_repo->insertOpenItem(makeItem(projectId, "Parent")).value();

    auto child = makeItem(projectId, "Child");
    child.parentItemId = parentId;
    auto childId = m_repo->insertOpenItem(child).value();

    EXPECT_TRUE(m_repo->removeOpenItem(parentId));
    EXPECT_FALSE(m_repo->findOpenItemById(parentId).has_value());
    EXPECT_FALSE(m_repo->findOpenItemById(childId).has_value());
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesModelAndGCodeItems) {
    auto projectId = insertProject("Existing Links");
    auto modelId = insertModel("Relief", "model-hash");
    auto gcodeId = insertGCode("Relief Finish", "gcode-hash");

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 4);

    auto modelItems = m_repo->findOpenItemsBySource(projectId, "models", modelId);
    ASSERT_EQ(modelItems.size(), 1u);
    EXPECT_EQ(modelItems[0].itemType, dw::ProjectOpenItemType::Model);
    EXPECT_EQ(modelItems[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(modelItems[0].displayName, "Relief");
    EXPECT_NE(modelItems[0].snapshotJson.find(R"("hash":"model-hash")"), std::string::npos);
    EXPECT_NE(modelItems[0].snapshotJson.find(R"("file_path":"/models/Relief.stl")"),
              std::string::npos);

    auto gcodeItems = m_repo->findOpenItemsBySource(projectId, "gcode_files", gcodeId);
    ASSERT_EQ(gcodeItems.size(), 1u);
    EXPECT_EQ(gcodeItems[0].itemType, dw::ProjectOpenItemType::Gcode);
    EXPECT_EQ(gcodeItems[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(gcodeItems[0].displayName, "Relief Finish");
    EXPECT_NE(gcodeItems[0].snapshotJson.find(R"("hash":"gcode-hash")"), std::string::npos);
    EXPECT_NE(gcodeItems[0].snapshotJson.find(R"("estimated_time":45)"), std::string::npos);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_IsIdempotent) {
    auto projectId = insertProject("No Duplicates");
    auto modelId = insertModel("Part", "part-hash");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 1);
    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 0);

    auto items = m_repo->listOpenItemsForProject(projectId);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].sourceId, modelId);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_RemovesItemsForDeletedLinks) {
    auto projectId = insertProject("Removed Links");
    auto modelId = insertModel("Part", "part-hash");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));
    ASSERT_EQ(m_repo->ensureOpenItemsForProject(projectId), 1);

    ASSERT_TRUE(m_repo->removeModel(projectId, modelId));
    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 0);

    EXPECT_TRUE(m_repo->listOpenItemsForProject(projectId).empty());
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_RemovesToolItemsForRemovedGCode) {
    auto projectId = insertProject("Removed G-code Tools");
    auto gcodeId = insertGCode("Toolpath", "toolpath-hash");

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));
    ASSERT_EQ(m_repo->ensureOpenItemsForProject(projectId), 3);

    ASSERT_TRUE(gcodeRepo.removeFromProject(projectId, gcodeId));
    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 0);
    EXPECT_TRUE(m_repo->listOpenItemsForProject(projectId).empty());
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesMaterialItemsForModelMaterials) {
    auto projectId = insertProject("Assigned Material");
    auto modelId = insertModel("Part", "part-hash");
    auto materialId = insertMaterial("Black Walnut");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));

    auto stmt = m_db.prepare("UPDATE models SET material_id = ? WHERE id = ?");
    ASSERT_TRUE(stmt.bindInt(1, materialId));
    ASSERT_TRUE(stmt.bindInt(2, modelId));
    ASSERT_TRUE(stmt.execute());

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 2);

    auto materialItems = m_repo->findOpenItemsBySource(projectId, "materials", materialId);
    ASSERT_EQ(materialItems.size(), 1u);
    EXPECT_EQ(materialItems[0].itemType, dw::ProjectOpenItemType::Material);
    EXPECT_EQ(materialItems[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(materialItems[0].displayName, "Black Walnut");
    EXPECT_NE(materialItems[0].snapshotJson.find(R"("name":"Black Walnut")"),
              std::string::npos);
    EXPECT_NE(materialItems[0].snapshotJson.find(R"("janka_hardness":1010)"),
              std::string::npos);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesStockItemsForProjectMaterials) {
    auto projectId = insertProject("Assigned Stock");
    auto modelId = insertModel("Part", "part-hash");
    auto materialId = insertMaterial("Black Walnut");
    auto stockId = insertStockSize(materialId, "Walnut blank");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));

    auto stmt = m_db.prepare("UPDATE models SET material_id = ? WHERE id = ?");
    ASSERT_TRUE(stmt.bindInt(1, materialId));
    ASSERT_TRUE(stmt.bindInt(2, modelId));
    ASSERT_TRUE(stmt.execute());

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 3);

    auto stockItems = m_repo->findOpenItemsBySource(projectId, "stock_sizes", stockId);
    ASSERT_EQ(stockItems.size(), 1u);
    EXPECT_EQ(stockItems[0].itemType, dw::ProjectOpenItemType::Stock);
    EXPECT_EQ(stockItems[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(stockItems[0].displayName, "Walnut blank");
    EXPECT_NE(stockItems[0].snapshotJson.find(R"("price_per_unit":24.5)"), std::string::npos);
    EXPECT_NE(stockItems[0].snapshotJson.find(R"("unit_label":"each")"), std::string::npos);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesOperationItemForDirectCarveGroup) {
    auto projectId = insertProject("Direct Carve");
    auto modelId = insertModel("Relief", "relief-hash");
    auto gcodeId = insertGCode("Relief Finish", "finish-hash");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));
    auto groupId = gcodeRepo.createGroup(modelId, "Direct Carve", 0);
    ASSERT_TRUE(groupId.has_value());
    ASSERT_TRUE(gcodeRepo.addToGroup(*groupId, gcodeId));

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 5);

    auto operationItems = m_repo->findOpenItemsBySource(projectId, "operation_groups", *groupId);
    ASSERT_EQ(operationItems.size(), 1u);
    EXPECT_EQ(operationItems[0].itemType, dw::ProjectOpenItemType::Operation);
    EXPECT_EQ(operationItems[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(operationItems[0].displayName, "Direct Carve");
    ASSERT_TRUE(operationItems[0].parentItemId.has_value());

    auto gcodeItems = m_repo->findOpenItemsBySource(projectId, "gcode_files", gcodeId);
    ASSERT_EQ(gcodeItems.size(), 1u);
    EXPECT_EQ(gcodeItems[0].parentItemId, operationItems[0].id);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesToolItemsFromGCodeToolNumbers) {
    auto projectId = insertProject("Tool Requirements");
    auto modelId = insertModel("Relief", "relief-hash");
    auto gcodeId = insertGCode("Relief Finish", "finish-hash");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));
    auto groupId = gcodeRepo.createGroup(modelId, "Direct Carve", 0);
    ASSERT_TRUE(groupId.has_value());
    ASSERT_TRUE(gcodeRepo.addToGroup(*groupId, gcodeId));

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 5);

    auto operationItems = m_repo->findOpenItemsBySource(projectId, "operation_groups", *groupId);
    ASSERT_EQ(operationItems.size(), 1u);

    auto firstTool = m_repo->findOpenItemsBySourceKey(projectId,
                                                       "gcode_files:" +
                                                           std::to_string(gcodeId) + ":tool:1");
    ASSERT_EQ(firstTool.size(), 1u);
    EXPECT_EQ(firstTool[0].itemType, dw::ProjectOpenItemType::Tool);
    EXPECT_EQ(firstTool[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(firstTool[0].displayName, "Tool 1");
    EXPECT_EQ(firstTool[0].parentItemId, operationItems[0].id);
    EXPECT_NE(firstTool[0].snapshotJson.find(R"("tool_number":1)"), std::string::npos);

    auto secondTool = m_repo->findOpenItemsBySourceKey(projectId,
                                                        "gcode_files:" +
                                                            std::to_string(gcodeId) + ":tool:3");
    ASSERT_EQ(secondTool.size(), 1u);
    EXPECT_EQ(secondTool[0].displayName, "Tool 3");
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesCutPlanAndCostItems) {
    auto projectId = insertProject("Plans and Costs");

    dw::CutPlanRepository cutPlanRepo(m_db);
    dw::CutPlanRecord plan;
    plan.projectId = projectId;
    plan.name = "Sheet Layout";
    plan.algorithm = "guillotine";
    plan.sheetConfigJson = R"({"width":2440,"height":1220})";
    plan.partsJson = R"([{"name":"Panel","quantity":2}])";
    plan.resultJson = R"({"placements":2})";
    plan.allowRotation = false;
    plan.kerf = 3.175f;
    plan.margin = 6.35f;
    plan.sheetsUsed = 1;
    plan.efficiency = 0.82f;
    auto planId = cutPlanRepo.insert(plan);
    ASSERT_TRUE(planId.has_value());

    dw::CostRepository costRepo(m_db);
    dw::CostingRecord cost;
    cost.name = "Project Estimate";
    cost.projectId = projectId;
    cost.subtotal = 125.0;
    cost.total = 135.0;
    auto costId = costRepo.insert(cost);
    ASSERT_TRUE(costId.has_value());

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 2);

    auto cutPlanItems = m_repo->findOpenItemsBySource(projectId, "cut_plans", *planId);
    ASSERT_EQ(cutPlanItems.size(), 1u);
    EXPECT_EQ(cutPlanItems[0].itemType, dw::ProjectOpenItemType::CutPlan);
    EXPECT_EQ(cutPlanItems[0].displayName, "Sheet Layout");
    EXPECT_NE(cutPlanItems[0].snapshotJson.find(R"("sheets_used":1)"), std::string::npos);
    EXPECT_NE(cutPlanItems[0].snapshotJson.find(R"("efficiency":0.82)"), std::string::npos);

    auto costItems = m_repo->findOpenItemsBySource(projectId, "costing_records", *costId);
    ASSERT_EQ(costItems.size(), 1u);
    EXPECT_EQ(costItems[0].itemType, dw::ProjectOpenItemType::Cost);
    EXPECT_EQ(costItems[0].displayName, "Project Estimate");
    EXPECT_NE(costItems[0].snapshotJson.find(R"("total":135)"), std::string::npos);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_CreatesJobItemsForProjectGCodeRuns) {
    auto projectId = insertProject("Runtime Trace");
    auto gcodeId = insertGCode("Relief Finish", "finish-hash");
    auto jobId = insertJobForGCode("Relief Finish", "completed");

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 4);

    auto gcodeItems = m_repo->findOpenItemsBySource(projectId, "gcode_files", gcodeId);
    ASSERT_EQ(gcodeItems.size(), 1u);

    auto jobItems = m_repo->findOpenItemsBySource(projectId, "cnc_jobs", jobId);
    ASSERT_EQ(jobItems.size(), 1u);
    EXPECT_EQ(jobItems[0].itemType, dw::ProjectOpenItemType::Job);
    EXPECT_EQ(jobItems[0].status, dw::ProjectOpenItemStatus::Complete);
    EXPECT_EQ(jobItems[0].parentItemId, gcodeItems[0].id);
    EXPECT_EQ(jobItems[0].displayName, "Run: Relief Finish.nc");
    EXPECT_NE(jobItems[0].snapshotJson.find(R"("elapsed_seconds":73.5)"), std::string::npos);
}

TEST_F(ProjectOpenItemRepoTest, EnsureOpenItemsForProject_UpdatesExistingJobItemStatus) {
    auto projectId = insertProject("Runtime Update");
    auto gcodeId = insertGCode("Relief Finish", "finish-hash");
    auto jobId = insertJobForGCode("Relief Finish", "running");

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));

    ASSERT_EQ(m_repo->ensureOpenItemsForProject(projectId), 4);
    auto runningItems = m_repo->findOpenItemsBySource(projectId, "cnc_jobs", jobId);
    ASSERT_EQ(runningItems.size(), 1u);
    EXPECT_EQ(runningItems[0].status, dw::ProjectOpenItemStatus::Sent);

    dw::JobRepository jobRepo(m_db);
    dw::ModalState modal;
    ASSERT_TRUE(jobRepo.finishJob(jobId, "completed", 100, 88.0f, 0, modal));

    EXPECT_EQ(m_repo->ensureOpenItemsForProject(projectId), 0);
    auto completedItems = m_repo->findOpenItemsBySource(projectId, "cnc_jobs", jobId);
    ASSERT_EQ(completedItems.size(), 1u);
    EXPECT_EQ(completedItems[0].id, runningItems[0].id);
    EXPECT_EQ(completedItems[0].status, dw::ProjectOpenItemStatus::Complete);
    EXPECT_NE(completedItems[0].snapshotJson.find(R"("elapsed_seconds":88)"), std::string::npos);
}

TEST_F(ProjectOpenItemRepoTest, ValidateOpenItemsForProject_MarksChangedSourcesStale) {
    auto projectId = insertProject("Changed Sources");
    auto modelId = insertModel("Relief", "model-v1");
    auto materialId = insertMaterial("Black Walnut");
    auto stockId = insertStockSize(materialId, "Walnut blank");
    auto gcodeId = insertGCode("Relief Finish", "gcode-v1");
    ASSERT_TRUE(m_repo->addModel(projectId, modelId));

    auto modelStmt = m_db.prepare("UPDATE models SET material_id = ? WHERE id = ?");
    ASSERT_TRUE(modelStmt.bindInt(1, materialId));
    ASSERT_TRUE(modelStmt.bindInt(2, modelId));
    ASSERT_TRUE(modelStmt.execute());

    dw::GCodeRepository gcodeRepo(m_db);
    ASSERT_TRUE(gcodeRepo.addToProject(projectId, gcodeId));
    ASSERT_EQ(m_repo->ensureOpenItemsForProject(projectId), 6);

    ASSERT_TRUE(m_db.execute("UPDATE models SET hash = 'model-v2' WHERE id = " +
                             std::to_string(modelId)));
    ASSERT_TRUE(m_db.execute("UPDATE materials SET janka_hardness = 1200 WHERE id = " +
                             std::to_string(materialId)));
    ASSERT_TRUE(m_db.execute("UPDATE stock_sizes SET price_per_unit = 30 WHERE id = " +
                             std::to_string(stockId)));
    ASSERT_TRUE(m_db.execute("UPDATE gcode_files SET hash = 'gcode-v2' WHERE id = " +
                             std::to_string(gcodeId)));

    EXPECT_EQ(m_repo->validateOpenItemsForProject(projectId), 4);

    EXPECT_EQ(m_repo->findOpenItemsBySource(projectId, "models", modelId)[0].status,
              dw::ProjectOpenItemStatus::Stale);
    EXPECT_EQ(m_repo->findOpenItemsBySource(projectId, "materials", materialId)[0].status,
              dw::ProjectOpenItemStatus::Stale);
    EXPECT_EQ(m_repo->findOpenItemsBySource(projectId, "stock_sizes", stockId)[0].status,
              dw::ProjectOpenItemStatus::Stale);
    EXPECT_EQ(m_repo->findOpenItemsBySource(projectId, "gcode_files", gcodeId)[0].status,
              dw::ProjectOpenItemStatus::Stale);
}

TEST_F(ProjectOpenItemRepoTest, ValidateOpenItemsForProject_MarksMissingSourcesMissing) {
    auto projectId = insertProject("Missing Source");
    auto item = makeItem(projectId, "Missing model");
    item.sourceId = 9999;
    auto itemId = m_repo->insertOpenItem(item);
    ASSERT_TRUE(itemId.has_value());

    EXPECT_EQ(m_repo->validateOpenItemsForProject(projectId), 1);

    auto updated = m_repo->findOpenItemById(*itemId);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->status, dw::ProjectOpenItemStatus::Missing);
}
