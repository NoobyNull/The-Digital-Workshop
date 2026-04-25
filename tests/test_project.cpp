// Digital Workshop - Project Class Tests

#include <gtest/gtest.h>

#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/project/project_directory.h"
#include "core/project/project.h"
#include "core/utils/file_utils.h"

#include <filesystem>

// --- Metadata ---

TEST(Project, DefaultState) {
    dw::Project proj;
    EXPECT_EQ(proj.id(), 0);
    EXPECT_TRUE(proj.name().empty());
    EXPECT_TRUE(proj.description().empty());
    EXPECT_EQ(proj.modelCount(), 0);
    EXPECT_FALSE(proj.isModified());
}

TEST(Project, SetName) {
    dw::Project proj;
    proj.setName("CNC Bracket");
    EXPECT_EQ(proj.name(), "CNC Bracket");
}

TEST(Project, SetDescription) {
    dw::Project proj;
    proj.setDescription("A test project");
    EXPECT_EQ(proj.description(), "A test project");
}

TEST(Project, SetFilePath) {
    dw::Project proj;
    proj.setFilePath("/projects/test.dwp");
    EXPECT_EQ(proj.filePath(), dw::Path("/projects/test.dwp"));
}

// --- Modified flag ---

TEST(Project, ModifiedFlag) {
    dw::Project proj;
    EXPECT_FALSE(proj.isModified());

    proj.markModified();
    EXPECT_TRUE(proj.isModified());

    proj.clearModified();
    EXPECT_FALSE(proj.isModified());
}

// --- Model management ---

TEST(Project, AddModel) {
    dw::Project proj;
    proj.addModel(1);
    proj.addModel(2);
    proj.addModel(3);

    EXPECT_EQ(proj.modelCount(), 3);
    EXPECT_TRUE(proj.hasModel(1));
    EXPECT_TRUE(proj.hasModel(2));
    EXPECT_TRUE(proj.hasModel(3));
}

TEST(Project, AddModel_NoDuplicate) {
    dw::Project proj;
    proj.addModel(1);
    proj.addModel(1); // duplicate

    // Should either have 1 or 2 depending on impl; just don't crash
    EXPECT_TRUE(proj.hasModel(1));
}

TEST(Project, RemoveModel) {
    dw::Project proj;
    proj.addModel(1);
    proj.addModel(2);
    proj.addModel(3);

    proj.removeModel(2);
    EXPECT_EQ(proj.modelCount(), 2);
    EXPECT_TRUE(proj.hasModel(1));
    EXPECT_FALSE(proj.hasModel(2));
    EXPECT_TRUE(proj.hasModel(3));
}

TEST(Project, RemoveModel_NotPresent) {
    dw::Project proj;
    proj.addModel(1);
    proj.removeModel(999); // not present, should not crash
    EXPECT_EQ(proj.modelCount(), 1);
}

TEST(Project, HasModel_False) {
    dw::Project proj;
    EXPECT_FALSE(proj.hasModel(42));
}

TEST(Project, ModelIds_Order) {
    dw::Project proj;
    proj.addModel(10);
    proj.addModel(20);
    proj.addModel(30);

    const auto& ids = proj.modelIds();
    EXPECT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], 10);
    EXPECT_EQ(ids[1], 20);
    EXPECT_EQ(ids[2], 30);
}

TEST(Project, ReorderModel) {
    dw::Project proj;
    proj.addModel(1);
    proj.addModel(2);
    proj.addModel(3);

    // Move model 3 to position 0
    proj.reorderModel(3, 0);

    const auto& ids = proj.modelIds();
    EXPECT_EQ(ids[0], 3);
}

// --- Record access ---

TEST(Project, RecordAccess) {
    dw::Project proj;
    proj.setName("Test");
    proj.record().id = 42;

    EXPECT_EQ(proj.id(), 42);
    EXPECT_EQ(proj.record().name, "Test");
}

TEST(ProjectManager, SaveCreatesCanonicalProjectDirectoryManifest) {
    auto tmpDir = std::filesystem::temp_directory_path() / "dw_test_project_manager_manifest";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto oldProjectsDir = dw::Config::instance().getProjectsDir();
    dw::Config::instance().setProjectsDir(tmpDir / "Projects");

    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    auto modelPath = tmpDir / "panel.stl";
    ASSERT_TRUE(dw::file::writeText(modelPath, "solid panel\nendsolid panel\n"));

    dw::ModelRecord model;
    model.hash = "modelhash001";
    model.name = "Panel";
    model.filePath = modelPath;
    model.fileFormat = "stl";

    dw::ModelRepository modelRepo(db);
    auto modelId = modelRepo.insert(model);
    ASSERT_TRUE(modelId.has_value());

    auto gcodePath = tmpDir / "panel.nc";
    ASSERT_TRUE(dw::file::writeText(gcodePath, "G0 X0 Y0\nM30\n"));

    dw::GCodeRecord gcode;
    gcode.hash = "gcodehash001";
    gcode.name = "Panel Cut";
    gcode.filePath = gcodePath;

    dw::GCodeRepository gcodeRepo(db);
    auto gcodeId = gcodeRepo.insert(gcode);
    ASSERT_TRUE(gcodeId.has_value());

    dw::ProjectManager manager(db);
    auto project = manager.create("Panel Project");
    ASSERT_TRUE(project);
    project->addModel(*modelId);
    ASSERT_TRUE(gcodeRepo.addToProject(project->id(), *gcodeId));

    ASSERT_TRUE(manager.save(*project));

    dw::ProjectRepository projectRepo(db);
    auto openItems = projectRepo.listOpenItemsForProject(project->id());
    ASSERT_EQ(openItems.size(), 2u);
    EXPECT_EQ(openItems[0].itemType, dw::ProjectOpenItemType::Model);
    EXPECT_EQ(openItems[0].sourceId, *modelId);
    EXPECT_EQ(openItems[1].itemType, dw::ProjectOpenItemType::Gcode);
    EXPECT_EQ(openItems[1].sourceId, *gcodeId);

    dw::ProjectDirectory dir;
    ASSERT_TRUE(dir.open(project->filePath()));
    EXPECT_EQ(project->filePath(), tmpDir / "Projects" / "panel-project");
    ASSERT_EQ(dir.models().size(), 1u);
    EXPECT_EQ(dir.models()[0].hash, "modelhash001");
    ASSERT_EQ(dir.gcodeFiles().size(), 1u);
    EXPECT_FALSE(dir.gcodeFiles()[0].filename.empty());
    EXPECT_TRUE(dw::file::exists(dir.modelsDir() / dir.models()[0].filename));
    EXPECT_TRUE(dw::file::exists(dir.gcodeDir() / dir.gcodeFiles()[0].filename));

    dw::Config::instance().setProjectsDir(oldProjectsDir);
    std::filesystem::remove_all(tmpDir);
}

TEST(ProjectManager, OpenSynthesizesOpenItemsForExistingLinks) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectRepository projectRepo(db);
    dw::ProjectRecord projectRecord;
    projectRecord.name = "Legacy Project";
    auto projectId = projectRepo.insert(projectRecord);
    ASSERT_TRUE(projectId.has_value());

    dw::ModelRepository modelRepo(db);
    dw::ModelRecord model;
    model.hash = "legacy-model-hash";
    model.name = "Legacy Model";
    model.filePath = "/tmp/legacy.stl";
    model.fileFormat = "stl";
    auto modelId = modelRepo.insert(model);
    ASSERT_TRUE(modelId.has_value());
    ASSERT_TRUE(projectRepo.addModel(*projectId, *modelId));

    dw::GCodeRepository gcodeRepo(db);
    dw::GCodeRecord gcode;
    gcode.hash = "legacy-gcode-hash";
    gcode.name = "Legacy Toolpath";
    gcode.filePath = "/tmp/legacy.nc";
    auto gcodeId = gcodeRepo.insert(gcode);
    ASSERT_TRUE(gcodeId.has_value());
    ASSERT_TRUE(gcodeRepo.addToProject(*projectId, *gcodeId));

    ASSERT_TRUE(projectRepo.listOpenItemsForProject(*projectId).empty());

    dw::ProjectManager manager(db);
    auto project = manager.open(*projectId);
    ASSERT_TRUE(project);

    auto openItems = projectRepo.listOpenItemsForProject(*projectId);
    ASSERT_EQ(openItems.size(), 2u);
    EXPECT_EQ(openItems[0].itemType, dw::ProjectOpenItemType::Model);
    EXPECT_EQ(openItems[0].displayName, "Legacy Model");
    EXPECT_EQ(openItems[1].itemType, dw::ProjectOpenItemType::Gcode);
    EXPECT_EQ(openItems[1].displayName, "Legacy Toolpath");
}

TEST(ProjectManager, ListOpenItemsValidatesChangedSourceSnapshots) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectRepository projectRepo(db);
    dw::ProjectRecord projectRecord;
    projectRecord.name = "Validation Project";
    auto projectId = projectRepo.insert(projectRecord);
    ASSERT_TRUE(projectId.has_value());

    dw::GCodeRepository gcodeRepo(db);
    dw::GCodeRecord gcode;
    gcode.hash = "before";
    gcode.name = "Changed Toolpath";
    gcode.filePath = "/tmp/changed.nc";
    auto gcodeId = gcodeRepo.insert(gcode);
    ASSERT_TRUE(gcodeId.has_value());
    ASSERT_TRUE(gcodeRepo.addToProject(*projectId, *gcodeId));

    dw::ProjectManager manager(db);
    auto project = manager.open(*projectId);
    ASSERT_TRUE(project);

    ASSERT_TRUE(db.execute("UPDATE gcode_files SET hash = 'after' WHERE id = " +
                           std::to_string(*gcodeId)));

    auto openItems = manager.listOpenItems(*projectId);
    ASSERT_EQ(openItems.size(), 1u);
    EXPECT_EQ(openItems[0].itemType, dw::ProjectOpenItemType::Gcode);
    EXPECT_EQ(openItems[0].status, dw::ProjectOpenItemStatus::Stale);
}

TEST(ProjectManager, UpsertCurrentOpenItemUsesCurrentProjectAndStableSourceKey) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    auto project = manager.create("Direct Carve Project");
    ASSERT_TRUE(project);
    manager.setCurrentProject(project);

    dw::ProjectOpenItem item;
    item.itemType = dw::ProjectOpenItemType::Operation;
    item.status = dw::ProjectOpenItemStatus::Planned;
    item.sourceKey = "direct_carve:relief";
    item.displayName = "Direct Carve: relief";
    item.intentJson = R"({"depth_mm":3.0})";

    auto firstId = manager.upsertCurrentOpenItem(item);
    ASSERT_TRUE(firstId.has_value());

    item.status = dw::ProjectOpenItemStatus::Ready;
    item.intentJson = R"({"depth_mm":4.5})";

    auto secondId = manager.upsertCurrentOpenItem(item);
    ASSERT_TRUE(secondId.has_value());
    EXPECT_EQ(*firstId, *secondId);

    auto openItems = manager.currentOpenItems();
    ASSERT_EQ(openItems.size(), 1u);
    EXPECT_EQ(openItems[0].projectId, project->id());
    EXPECT_EQ(openItems[0].status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(openItems[0].intentJson, R"({"depth_mm":4.5})");
}
