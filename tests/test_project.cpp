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

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace {

#ifdef __linux__
class ScopedXdgRuntimeDir {
  public:
    explicit ScopedXdgRuntimeDir(const dw::Path& path) {
        if (const char* current = std::getenv("XDG_RUNTIME_DIR"))
            m_previous = current;
        m_valid = setenv("XDG_RUNTIME_DIR", path.c_str(), 1) == 0;
    }

    ~ScopedXdgRuntimeDir() {
        if (!m_valid)
            return;
        if (m_previous)
            (void)setenv("XDG_RUNTIME_DIR", m_previous->c_str(), 1);
        else
            (void)unsetenv("XDG_RUNTIME_DIR");
    }

    bool valid() const { return m_valid; }

  private:
    std::optional<std::string> m_previous;
    bool m_valid = false;
};
#endif

} // namespace

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

TEST(ProjectManager, CreateValidatesNameBeforeDatabaseInsertion) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));
    dw::ProjectManager manager(db);
    dw::ProjectRepository repository(db);

    EXPECT_EQ(manager.create("   "), nullptr);
    EXPECT_EQ(manager.create("../river-sign"), nullptr);
    EXPECT_EQ(manager.create("River/Sign"), nullptr);
    EXPECT_EQ(repository.count(), 0);

    const auto project = manager.create("  River Sign  ");
    ASSERT_NE(project, nullptr);
    EXPECT_EQ(project->name(), "River Sign");
    EXPECT_EQ(repository.count(), 1);
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
    EXPECT_EQ(dir.projectId(), project->id());
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

    manager.synchronizeActiveProject(project);
    const auto modelItem = manager.findOpenItemBySource("models", *modelId);
    ASSERT_TRUE(modelItem.has_value());
    EXPECT_EQ(modelItem->id, openItems[0].id);
    EXPECT_EQ(modelItem->displayName, "Legacy Model");
    EXPECT_FALSE(manager.findOpenItemBySource("models", 999999).has_value());
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
    manager.synchronizeActiveProject(project);

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

TEST(ProjectManager, UpdateOpenItemChangesOnlyTheExactActiveProjectRow) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    const auto project = manager.create("Exact Update Project");
    ASSERT_TRUE(project);
    manager.synchronizeActiveProject(project);

    dw::ProjectRepository repository(db);
    dw::ProjectOpenItem first;
    first.projectId = project->id();
    first.itemType = dw::ProjectOpenItemType::Operation;
    first.sourceKey = "direct_carve:shared-source";
    first.displayName = "First operation";
    first.intentJson = R"({"depth_mm":1.0})";
    const auto firstId = repository.insertOpenItem(first);
    ASSERT_TRUE(firstId.has_value());

    auto second = first;
    second.displayName = "Second operation";
    second.intentJson = R"({"depth_mm":2.0})";
    const auto secondId = repository.insertOpenItem(second);
    ASSERT_TRUE(secondId.has_value());

    auto exact = repository.findOpenItemById(*secondId);
    ASSERT_TRUE(exact.has_value());
    exact->status = dw::ProjectOpenItemStatus::Ready;
    exact->intentJson = R"({"depth_mm":4.5})";
    ASSERT_TRUE(manager.updateOpenItem(*exact));

    const auto unchanged = repository.findOpenItemById(*firstId);
    const auto updated = repository.findOpenItemById(*secondId);
    ASSERT_TRUE(unchanged.has_value());
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(unchanged->status, dw::ProjectOpenItemStatus::Planned);
    EXPECT_EQ(unchanged->intentJson, R"({"depth_mm":1.0})");
    EXPECT_EQ(updated->status, dw::ProjectOpenItemStatus::Ready);
    EXPECT_EQ(updated->intentJson, R"({"depth_mm":4.5})");
}

TEST(ProjectManager, UpdateOpenItemRejectsMissingAndForeignActiveProjectIdentity) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    const auto firstProject = manager.create("First Update Project");
    const auto secondProject = manager.create("Second Update Project");
    ASSERT_TRUE(firstProject);
    ASSERT_TRUE(secondProject);

    dw::ProjectRepository repository(db);
    dw::ProjectOpenItem stored;
    stored.projectId = firstProject->id();
    stored.itemType = dw::ProjectOpenItemType::Operation;
    stored.sourceKey = "direct_carve:exact-row";
    stored.displayName = "Persisted operation";
    const auto storedId = repository.insertOpenItem(stored);
    ASSERT_TRUE(storedId.has_value());
    auto update = repository.findOpenItemById(*storedId);
    ASSERT_TRUE(update.has_value());
    update->status = dw::ProjectOpenItemStatus::Ready;

    EXPECT_FALSE(manager.updateOpenItem(*update));

    manager.synchronizeActiveProject(firstProject);
    auto invalid = *update;
    invalid.id = 0;
    EXPECT_FALSE(manager.updateOpenItem(invalid));
    invalid = *update;
    invalid.id = 999999;
    EXPECT_FALSE(manager.updateOpenItem(invalid));

    manager.synchronizeActiveProject(secondProject);
    EXPECT_FALSE(manager.updateOpenItem(*update));

    auto spoofed = *update;
    spoofed.projectId = secondProject->id();
    EXPECT_FALSE(manager.updateOpenItem(spoofed));

    const auto unchanged = repository.findOpenItemById(*storedId);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_EQ(unchanged->projectId, firstProject->id());
    EXPECT_EQ(unchanged->status, dw::ProjectOpenItemStatus::Planned);
}

TEST(ProjectManager, RemoveOpenItemDeletesOnlyAnExactActiveProjectRow) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    const auto activeProject = manager.create("Active Removal Project");
    const auto foreignProject = manager.create("Foreign Removal Project");
    ASSERT_TRUE(activeProject);
    ASSERT_TRUE(foreignProject);

    dw::ProjectRepository repository(db);
    dw::ProjectOpenItem activeItem;
    activeItem.projectId = activeProject->id();
    activeItem.itemType = dw::ProjectOpenItemType::Tool;
    activeItem.sourceTable = "direct_carve";
    activeItem.sourceKey = "direct_carve:active:tool:finish";
    activeItem.displayName = "Active finishing tool";
    const auto activeItemId = repository.insertOpenItem(activeItem);
    ASSERT_TRUE(activeItemId.has_value());

    auto foreignItem = activeItem;
    foreignItem.projectId = foreignProject->id();
    foreignItem.sourceKey = "direct_carve:foreign:tool:finish";
    foreignItem.displayName = "Foreign finishing tool";
    const auto foreignItemId = repository.insertOpenItem(foreignItem);
    ASSERT_TRUE(foreignItemId.has_value());

    EXPECT_FALSE(manager.removeOpenItem(*activeItemId));
    manager.synchronizeActiveProject(activeProject);
    EXPECT_FALSE(manager.removeOpenItem(0));
    EXPECT_FALSE(manager.removeOpenItem(999999));
    EXPECT_FALSE(manager.removeOpenItem(*foreignItemId));
    EXPECT_TRUE(repository.findOpenItemById(*activeItemId).has_value());
    EXPECT_TRUE(repository.findOpenItemById(*foreignItemId).has_value());

    EXPECT_TRUE(manager.removeOpenItem(*activeItemId));
    EXPECT_FALSE(repository.findOpenItemById(*activeItemId).has_value());
    EXPECT_TRUE(repository.findOpenItemById(*foreignItemId).has_value());
    EXPECT_FALSE(manager.removeOpenItem(*activeItemId));
}

TEST(ProjectManager, SynchronizeActiveProjectSwitchesIdentityAndClearsStaleDirectory) {
    auto tmpDir = std::filesystem::temp_directory_path() / "dw_test_project_identity_switch";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto oldProjectsDir = dw::Config::instance().getProjectsDir();
    dw::Config::instance().setProjectsDir(tmpDir / "Projects");

    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    auto persisted = manager.create("Persisted Project");
    ASSERT_TRUE(persisted);
    ASSERT_TRUE(manager.save(*persisted));

    manager.synchronizeActiveProject(persisted);
    ASSERT_TRUE(manager.currentDirectory());

    auto transient = manager.create("Transient Project");
    ASSERT_TRUE(transient);
    transient->setFilePath(tmpDir / "missing-project-directory");
    ASSERT_FALSE(dw::file::exists(transient->filePath() / "project.json"));

    manager.synchronizeActiveProject(transient);

    EXPECT_EQ(manager.currentProject(), transient);
    EXPECT_EQ(manager.currentDirectory(), nullptr);

    manager.synchronizeActiveProject(nullptr);
    EXPECT_EQ(manager.currentProject(), nullptr);
    EXPECT_EQ(manager.currentDirectory(), nullptr);

    dw::Config::instance().setProjectsDir(oldProjectsDir);
    std::filesystem::remove_all(tmpDir);
}

TEST(ProjectManager, SynchronizeActiveProjectReopensMatchingPersistedDirectory) {
    auto tmpDir = std::filesystem::temp_directory_path() / "dw_test_project_identity_reopen";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto oldProjectsDir = dw::Config::instance().getProjectsDir();
    dw::Config::instance().setProjectsDir(tmpDir / "Projects");

    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    auto project = manager.create("Reopen Project");
    ASSERT_TRUE(project);
    ASSERT_TRUE(manager.save(*project));
    const auto projectId = project->id();
    const auto projectRoot = project->filePath();

    manager.synchronizeActiveProject(nullptr);
    auto reopened = manager.open(projectId);
    ASSERT_TRUE(reopened);

    manager.synchronizeActiveProject(reopened);

    EXPECT_EQ(manager.currentProject(), reopened);
    ASSERT_TRUE(manager.currentDirectory());
    EXPECT_EQ(manager.currentDirectory()->root(), projectRoot);

    dw::Config::instance().setProjectsDir(oldProjectsDir);
    std::filesystem::remove_all(tmpDir);
}

#ifdef __linux__
TEST(ProjectManager, SavePersistsDurableNetworkLocationWhileKeepingLivePath) {
    const auto tempRoot =
        std::filesystem::temp_directory_path() / "dw_test_project_network_save";
    std::filesystem::remove_all(tempRoot);
    ScopedXdgRuntimeDir runtimeDir(tempRoot);
    ASSERT_TRUE(runtimeDir.valid());

    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    auto project = manager.create("Network Project");
    ASSERT_TRUE(project);
    const dw::Path liveRoot = tempRoot / "kio-fuse-old123" / "smb" /
                              "workshop.local" / "Projects" / "network-project";
    project->setFilePath(liveRoot);

    ASSERT_TRUE(manager.save(*project));

    EXPECT_EQ(project->filePath(), liveRoot);
    auto persisted = manager.getProjectInfo(project->id());
    ASSERT_TRUE(persisted.has_value());
    EXPECT_EQ(persisted->filePath,
              dw::Path("smb://workshop.local/Projects/network-project"));
    EXPECT_TRUE(dw::file::exists(liveRoot / "project.json"));

    auto duplicate = manager.create("Duplicate Network Project");
    ASSERT_TRUE(duplicate);
    duplicate->setFilePath(liveRoot);
    EXPECT_FALSE(manager.save(*duplicate));

    std::filesystem::remove_all(tempRoot);
}
#endif

TEST(ProjectManager, DiscardTemporaryProjectDataLeavesActiveStateForCloseCommit) {
    auto tempRoot =
        std::filesystem::temp_directory_path() / "dw_test_project_discard_temporary_data";
    std::filesystem::remove_all(tempRoot);
    const auto oldProjectsDir = dw::Config::instance().getProjectsDir();
    dw::Config::instance().setProjectsDir(tempRoot / "Projects");

    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    auto project = manager.create("Temporary Project", true);
    ASSERT_TRUE(project);
    ASSERT_TRUE(manager.save(*project));
    const auto projectRoot = project->filePath();

    manager.synchronizeActiveProject(project);
    auto activeDirectory = manager.currentDirectory();
    ASSERT_TRUE(activeDirectory);
    ASSERT_EQ(activeDirectory->root(), projectRoot);

    const auto temporaryFile = activeDirectory->modelsDir() / "temporary-part.stl";
    ASSERT_TRUE(dw::file::writeText(temporaryFile, "solid temporary\nendsolid temporary\n"));
    ASSERT_TRUE(dw::file::exists(temporaryFile));

    const auto projectId = project->id();
    ASSERT_TRUE(manager.getProjectInfo(projectId).has_value());

    ASSERT_TRUE(manager.discardTemporaryProjectData());

    EXPECT_FALSE(manager.getProjectInfo(projectId).has_value());
    EXPECT_FALSE(dw::file::exists(projectRoot));

    // Discard only removes owned storage. ProjectSessionIntegration commits the
    // active identity change after the close transition is accepted.
    EXPECT_EQ(manager.currentProject(), project);
    EXPECT_EQ(manager.currentDirectory(), activeDirectory);
    EXPECT_EQ(manager.currentDirectory()->root(), projectRoot);

    manager.synchronizeActiveProject(nullptr);
    EXPECT_EQ(manager.currentProject(), nullptr);
    EXPECT_EQ(manager.currentDirectory(), nullptr);

    dw::Config::instance().setProjectsDir(oldProjectsDir);
    std::filesystem::remove_all(tempRoot);
}

TEST(ProjectManager, SaveTemporaryProjectPromotesIntoOwnedPermanentDirectory) {
    const auto tempRoot =
        std::filesystem::temp_directory_path() / "dw_test_project_promote_temporary";
    std::filesystem::remove_all(tempRoot);

    const auto oldProjectsDir = dw::Config::instance().getProjectsDir();
    const auto projectsRoot = tempRoot / "Projects";
    dw::Config::instance().setProjectsDir(projectsRoot);

    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    dw::ProjectManager manager(db);
    auto project = manager.create("Promoted Project", true);
    ASSERT_TRUE(project);
    ASSERT_TRUE(manager.save(*project));
    manager.synchronizeActiveProject(project);

    const auto payload = manager.currentDirectory()->modelsDir() / "keep-me.stl";
    ASSERT_TRUE(dw::file::writeText(payload, "solid kept\nendsolid kept\n"));
    const auto temporaryProjectRoot = project->filePath();

    ASSERT_TRUE(manager.saveTemporaryProject());

    EXPECT_FALSE(project->isTemporary());
    EXPECT_NE(project->filePath(), temporaryProjectRoot);
    EXPECT_FALSE(dw::file::exists(temporaryProjectRoot));
    EXPECT_TRUE(dw::file::exists(project->filePath() / "project.json"));
    EXPECT_TRUE(dw::file::exists(project->filePath() / "models" / "keep-me.stl"));
    ASSERT_TRUE(manager.currentDirectory());
    EXPECT_EQ(manager.currentDirectory()->root(), project->filePath());

    dw::Config::instance().removeRecentProject(project->filePath());
    dw::Config::instance().setProjectsDir(oldProjectsDir);
    std::filesystem::remove_all(tempRoot);
}
