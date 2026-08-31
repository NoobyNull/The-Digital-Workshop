// Digital Workshop - Project Export Manager Tests

#include <gtest/gtest.h>

#include "core/database/cost_repository.h"
#include "core/database/cut_plan_repository.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/material_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/config/config.h"
#include "core/export/project_export_manager.h"
#include "core/library/library_manager.h"
#include "core/loaders/gcode_loader.h"
#include "core/paths/app_paths.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/utils/file_utils.h"

#include <filesystem>
#include <miniz.h>
#include <nlohmann/json.hpp>

namespace {

class ProjectExportTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_baseDir = std::filesystem::temp_directory_path() / "dw_test_export";
        std::filesystem::remove_all(m_baseDir);
        std::filesystem::create_directories(m_baseDir);

        m_modelsDir = m_baseDir / "models";
        std::filesystem::create_directories(m_modelsDir);

        m_previousModelsDir = dw::Config::instance().getModelsDir();
        m_previousGCodeDir = dw::Config::instance().getGCodeDir();
        m_previousProjectsDir = dw::Config::instance().getProjectsDir();
        dw::Config::instance().setModelsDir(m_modelsDir);
        dw::Config::instance().setGCodeDir(m_baseDir / "gcode");
        dw::Config::instance().setProjectsDir(m_baseDir / "projects");

        m_archivePath = (m_baseDir / "test.dwproj").string();

        // Open in-memory DB and initialize schema
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
    }

    void TearDown() override {
        dw::Config::instance().setModelsDir(m_previousModelsDir);
        dw::Config::instance().setGCodeDir(m_previousGCodeDir);
        dw::Config::instance().setProjectsDir(m_previousProjectsDir);
        std::filesystem::remove_all(m_baseDir);
    }

    // Create a dummy model file and insert a ModelRecord
    dw::i64 insertModelWithFile(const std::string& hash,
                                const std::string& name,
                                const std::string& ext = ".stl") {
        // Write a small binary blob to disk
        dw::Path filePath = m_modelsDir / (hash + ext);
        std::string content = "BINARYDATA_" + hash;
        EXPECT_TRUE(dw::file::writeText(filePath, content));

        dw::ModelRecord rec;
        rec.hash = hash;
        rec.name = name;
        rec.filePath = filePath;
        rec.fileFormat = ext.substr(1); // strip dot
        rec.fileSize = content.size();
        rec.vertexCount = 100;
        rec.triangleCount = 50;
        rec.boundsMin = dw::Vec3(-1.0f, -2.0f, -3.0f);
        rec.boundsMax = dw::Vec3(1.0f, 2.0f, 3.0f);
        rec.tags = {"tag1", "tag2"};

        dw::ModelRepository repo(m_db);
        auto id = repo.insert(rec);
        EXPECT_TRUE(id.has_value());
        return id.value_or(0);
    }

    // Create a project with linked models, return the Project object
    std::shared_ptr<dw::Project> createProjectWithModels(
        const std::string& projectName,
        const std::vector<std::pair<std::string, std::string>>& models) {
        dw::ProjectRepository projRepo(m_db);

        dw::ProjectRecord projRec;
        projRec.name = projectName;
        projRec.description = "Test project";
        auto projId = projRepo.insert(projRec);
        EXPECT_TRUE(projId.has_value());

        for (const auto& [hash, name] : models) {
            auto modelId = insertModelWithFile(hash, name);
            EXPECT_TRUE(projRepo.addModel(*projId, modelId));
        }

        // Build a Project object
        auto project = std::make_shared<dw::Project>();
        project->record().id = *projId;
        project->record().name = projectName;

        auto modelIds = projRepo.getModelIds(*projId);
        for (auto mid : modelIds) {
            project->addModel(mid);
        }

        return project;
    }

    // Manually create a .dwproj ZIP with custom manifest JSON
    void createArchiveWithManifest(
        const std::string& archivePath,
        const nlohmann::json& manifest,
        const std::vector<std::pair<std::string, std::string>>& blobEntries = {}) {
        mz_zip_archive zip{};
        ASSERT_TRUE(mz_zip_writer_init_file(&zip, archivePath.c_str(), 0));

        std::string manifestStr = manifest.dump(2);
        ASSERT_TRUE(mz_zip_writer_add_mem(
            &zip, "manifest.json", manifestStr.data(), manifestStr.size(), MZ_DEFAULT_COMPRESSION));

        for (const auto& [path, data] : blobEntries) {
            ASSERT_TRUE(mz_zip_writer_add_mem(
                &zip, path.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION));
        }

        ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
        mz_zip_writer_end(&zip);
    }

    std::filesystem::path m_baseDir;
    dw::Path m_modelsDir;
    dw::Path m_previousModelsDir;
    dw::Path m_previousGCodeDir;
    dw::Path m_previousProjectsDir;
    std::string m_archivePath;
    dw::Database m_db;
};

} // namespace

// --- Test 1: Export creates valid ZIP with manifest.json ---

TEST_F(ProjectExportTest, ExportCreatesValidZipWithManifest) {
    auto project = createProjectWithModels(
        "Test Export Project", {{"aabbccdd1111", "Widget"}, {"eeff00112233", "Bracket"}});

    dw::ProjectExportManager exporter(m_db);
    auto result = exporter.exportProject(*project, m_archivePath);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.modelCount, 2);
    EXPECT_GT(result.totalBytes, 0u);

    // Verify the .dwproj file exists
    EXPECT_TRUE(dw::file::exists(m_archivePath));

    // Open ZIP and verify manifest.json exists
    mz_zip_archive zip{};
    ASSERT_TRUE(mz_zip_reader_init_file(&zip, m_archivePath.c_str(), 0));

    size_t manifestSize = 0;
    void* manifestData =
        mz_zip_reader_extract_file_to_heap(&zip, "manifest.json", &manifestSize, 0);
    ASSERT_NE(manifestData, nullptr);

    std::string manifestStr(static_cast<const char*>(manifestData), manifestSize);
    mz_free(manifestData);
    mz_zip_reader_end(&zip);

    // Parse and validate manifest
    auto j = nlohmann::json::parse(manifestStr);
    EXPECT_EQ(j["format_version"].get<int>(), 2);
    EXPECT_EQ(j["project_name"].get<std::string>(), "Test Export Project");
    EXPECT_TRUE(j.contains("app_version"));
    EXPECT_TRUE(j.contains("created_at"));
    EXPECT_TRUE(j.contains("project_id"));

    auto models = j["models"];
    ASSERT_EQ(models.size(), 2u);

    for (const auto& m : models) {
        EXPECT_TRUE(m.contains("hash"));
        EXPECT_TRUE(m.contains("name"));
        EXPECT_TRUE(m.contains("file_in_archive"));
    }
}

// --- Test 2: Import round-trip preserves model metadata ---

TEST_F(ProjectExportTest, ImportRoundTripPreservesMetadata) {
    auto project = createProjectWithModels(
        "Roundtrip Project", {{"hash_alpha_001", "Alpha Model"}, {"hash_beta_0002", "Beta Model"}});

    dw::ProjectExportManager exporter(m_db);

    // Export
    auto exportResult = exporter.exportProject(*project, m_archivePath);
    ASSERT_TRUE(exportResult.success) << exportResult.error;

    // Create a second database (simulating a different machine)
    dw::Database db2;
    ASSERT_TRUE(db2.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db2));

    dw::ProjectExportManager importer(db2);
    auto importResult = importer.importProject(m_archivePath);

    ASSERT_TRUE(importResult.success) << importResult.error;
    EXPECT_EQ(importResult.modelCount, 2);

    // Verify project was created
    dw::ProjectRepository projRepo2(db2);
    auto projects = projRepo2.findAll();
    ASSERT_EQ(projects.size(), 1u);
    EXPECT_EQ(projects[0].name, "Roundtrip Project");

    // Verify models were linked
    auto modelIds = projRepo2.getModelIds(projects[0].id);
    EXPECT_EQ(modelIds.size(), 2u);

    // Verify model metadata
    dw::ModelRepository modelRepo2(db2);
    for (auto mid : modelIds) {
        auto model = modelRepo2.findById(mid);
        ASSERT_TRUE(model.has_value());
        EXPECT_FALSE(model->hash.empty());
        EXPECT_FALSE(model->name.empty());
        EXPECT_EQ(model->vertexCount, 100u);
        EXPECT_EQ(model->triangleCount, 50u);
        // Verify blob file exists on disk (filePath may be relative after v10 migration)
        EXPECT_TRUE(dw::file::exists(
            dw::PathResolver::resolve(model->filePath, dw::PathCategory::Models)));
    }
}

// --- Test 3: Import ignores unknown manifest fields (forward compat) ---

TEST_F(ProjectExportTest, ImportIgnoresUnknownManifestFields) {
    // Build a manifest with an unknown future field
    nlohmann::json manifest;
    manifest["format_version"] = 1;
    manifest["app_version"] = "2.0.0";
    manifest["created_at"] = "2026-01-01T00:00:00Z";
    manifest["project_id"] = 42;
    manifest["project_name"] = "Future Project";
    manifest["future_feature"] = true;
    manifest["another_unknown"] = {{"nested", "data"}};

    nlohmann::json model;
    model["name"] = "Future Model";
    model["hash"] = "futurehash001";
    model["original_filename"] = "future.stl";
    model["file_in_archive"] = "models/futurehash001.stl";
    model["file_format"] = "stl";
    model["tags"] = nlohmann::json::array();
    model["vertex_count"] = 10;
    model["triangle_count"] = 5;
    model["bounds_min"] = {0.0, 0.0, 0.0};
    model["bounds_max"] = {1.0, 1.0, 1.0};
    model["unknown_model_field"] = "should be ignored";
    manifest["models"] = nlohmann::json::array({model});

    // Create archive with blob
    std::string blobContent = "FAKE_STL_BLOB";
    createArchiveWithManifest(m_archivePath, manifest, {{"models/futurehash001.stl", blobContent}});

    dw::ProjectExportManager importer(m_db);
    auto result = importer.importProject(m_archivePath);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.modelCount, 1);

    // Verify project was created correctly
    dw::ProjectRepository projRepo(m_db);
    auto projects = projRepo.findAll();
    ASSERT_EQ(projects.size(), 1u);
    EXPECT_EQ(projects[0].name, "Future Project");
}

TEST_F(ProjectExportTest, LegacyProjectLibraryImportedGCodeAndExternalNcRemainUsable) {
    const std::string importedGCode =
        "G21\nG90\nT3 M6\nG0 X0 Y0 Z5\nG1 X10 Y0 Z0 F300\nG1 X10 Y10 Z0\nM30\n";

    nlohmann::json manifest;
    manifest["format_version"] = 1;
    manifest["app_version"] = "1.1.0";
    manifest["created_at"] = "2025-01-15T10:30:00Z";
    manifest["project_id"] = 27;
    manifest["project_name"] = "Legacy River Sign";
    manifest["models"] = nlohmann::json::array(
        {{{"name", "River Sign"},
          {"hash", "legacy-river-sign-model"},
          {"original_filename", "river-sign.stl"},
          {"file_in_archive", "models/legacy-river-sign.stl"},
          {"file_format", "stl"},
          {"tags", nlohmann::json::array({"sign", "legacy"})},
          {"vertex_count", 12},
          {"triangle_count", 4},
          {"bounds_min", {0.0, 0.0, 0.0}},
          {"bounds_max", {120.0, 60.0, 6.0}}}});
    manifest["gcode"] = nlohmann::json::array(
        {{{"id", 31},
          {"name", "Legacy Finish Pass"},
          {"hash", "legacy-river-sign-finish"},
          {"file_in_archive", "gcode/legacy-finish.nc"},
          {"estimated_time", 42.0},
          {"tool_numbers", nlohmann::json::array({3})}}});

    createArchiveWithManifest(
        m_archivePath,
        manifest,
        {{"models/legacy-river-sign.stl", "solid river_sign\nendsolid river_sign\n"},
         {"gcode/legacy-finish.nc", importedGCode}});

    dw::ProjectExportManager importer(m_db);
    const auto imported = importer.importProject(m_archivePath);
    ASSERT_TRUE(imported.success) << imported.error;
    ASSERT_TRUE(imported.importedProjectId.has_value());

    dw::ProjectManager projects(m_db);
    auto legacyProject = projects.open(*imported.importedProjectId);
    ASSERT_NE(legacyProject, nullptr);
    EXPECT_EQ(legacyProject->name(), "Legacy River Sign");
    ASSERT_EQ(legacyProject->modelCount(), 1);
    projects.synchronizeActiveProject(legacyProject);
    ASSERT_TRUE(projects.save(*legacyProject));
    EXPECT_EQ(projects.validateProjectStorage(legacyProject->id()),
              dw::ProjectStorageValidationStatus::Ready);

    dw::LibraryManager library(m_db);
    const auto libraryModel = library.getModelByHash("legacy-river-sign-model");
    ASSERT_TRUE(libraryModel.has_value());
    EXPECT_EQ(libraryModel->name, "River Sign");
    EXPECT_EQ(library.modelCount(), 1);

    dw::GCodeRepository gcodes(m_db);
    const auto projectGCode = gcodes.findByProject(legacyProject->id());
    ASSERT_EQ(projectGCode.size(), 1u);
    EXPECT_EQ(projectGCode.front().name, "Legacy Finish Pass");
    const auto importedGCodePath =
        dw::PathResolver::resolve(projectGCode.front().filePath, dw::PathCategory::GCode);
    ASSERT_TRUE(dw::file::isFile(importedGCodePath));

    dw::GCodeLoader loader;
    const auto importedLoad = loader.load(importedGCodePath);
    ASSERT_TRUE(importedLoad.success()) << importedLoad.error;
    EXPECT_GT(importedLoad.mesh->vertexCount(), 0u);
    ASSERT_EQ(loader.lastMetadata().toolNumbers.size(), 1u);
    EXPECT_EQ(loader.lastMetadata().toolNumbers.front(), 3);

    const dw::Path externalPath = m_baseDir / "external-one-off.nc";
    ASSERT_TRUE(dw::file::writeText(
        externalPath, "G21\nG90\nG0 X0 Y0 Z2\nG1 X5 Y5 Z0 F150\nM30\n"));
    const auto repositoryCountBefore = gcodes.count();
    EXPECT_FALSE(gcodes.findByPath(externalPath).has_value());
    const auto externalLoad = loader.load(externalPath);
    ASSERT_TRUE(externalLoad.success()) << externalLoad.error;
    EXPECT_GT(externalLoad.mesh->vertexCount(), 0u);
    EXPECT_EQ(gcodes.count(), repositoryCountBefore);
    EXPECT_FALSE(gcodes.findByPath(externalPath).has_value());
}

// --- Test 4: Import deduplicates existing models ---

TEST_F(ProjectExportTest, ImportDeduplicatesExistingModels) {
    const std::string sharedHash = "dedup_hash_shared";

    // Pre-insert a model with the same hash into the DB
    insertModelWithFile(sharedHash, "Pre-existing Model");

    // Build manifest referencing a model with the same hash
    nlohmann::json manifest;
    manifest["format_version"] = 1;
    manifest["app_version"] = "1.1.0";
    manifest["created_at"] = "2026-01-01T00:00:00Z";
    manifest["project_id"] = 1;
    manifest["project_name"] = "Dedup Test Project";

    nlohmann::json model;
    model["name"] = "Same Model Different Name";
    model["hash"] = sharedHash;
    model["original_filename"] = "same.stl";
    model["file_in_archive"] = "models/" + sharedHash + ".stl";
    model["file_format"] = "stl";
    model["tags"] = nlohmann::json::array();
    model["vertex_count"] = 200;
    model["triangle_count"] = 100;
    model["bounds_min"] = {0.0, 0.0, 0.0};
    model["bounds_max"] = {1.0, 1.0, 1.0};
    manifest["models"] = nlohmann::json::array({model});

    std::string blobContent = "FAKE_BLOB_DEDUP";
    createArchiveWithManifest(m_archivePath,
                              manifest,
                              {{"models/" + sharedHash + ".stl", blobContent}});

    dw::ProjectExportManager importer(m_db);
    auto result = importer.importProject(m_archivePath);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.modelCount, 1);

    // Verify only ONE model with this hash exists (not duplicated)
    dw::ModelRepository modelRepo(m_db);
    auto all = modelRepo.findAll();
    int hashCount = 0;
    for (const auto& m : all) {
        if (m.hash == sharedHash)
            hashCount++;
    }
    EXPECT_EQ(hashCount, 1) << "Model should not be duplicated";

    // Verify the project still links to the model
    dw::ProjectRepository projRepo(m_db);
    auto projects = projRepo.findAll();
    // Find the imported project (not any pre-existing ones)
    bool found = false;
    for (const auto& p : projects) {
        if (p.name == "Dedup Test Project") {
            auto modelIds = projRepo.getModelIds(p.id);
            EXPECT_EQ(modelIds.size(), 1u);
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Imported project should exist";
}

// --- Test 5: Export/import round-trip preserves materials and thumbnails ---

TEST_F(ProjectExportTest, RoundTripPreservesMaterialsAndThumbnails) {
    // 1. Insert a material
    dw::MaterialRepository matRepo(m_db);
    dw::MaterialRecord matRec;
    matRec.name = "Red Oak";
    matRec.category = dw::MaterialCategory::Hardwood;

    // Create a fake .dwmat file on disk
    dw::Path matArchivePath = m_baseDir / "red_oak.dwmat";
    std::string matContent = "FAKE_DWMAT_ARCHIVE_BYTES";
    ASSERT_TRUE(dw::file::writeText(matArchivePath, matContent));
    matRec.archivePath = matArchivePath;

    auto matId = matRepo.insert(matRec);
    ASSERT_TRUE(matId.has_value());

    // 2. Insert a model
    const std::string modelHash = "mat_thumb_test_hash";
    auto modelId = insertModelWithFile(modelHash, "Oak Widget");

    // 3. Write a small thumbnail PNG and assign it
    dw::Path thumbPath = m_baseDir / "thumbnails" / (modelHash + ".png");
    std::filesystem::create_directories(m_baseDir / "thumbnails");
    std::string thumbContent = "FAKEPNGDATA_1x1";
    ASSERT_TRUE(dw::file::writeText(thumbPath, thumbContent));

    dw::ModelRepository modelRepo(m_db);
    ASSERT_TRUE(modelRepo.updateThumbnail(modelId, thumbPath));

    // 4. Assign material to model via raw SQL
    {
        auto stmt = m_db.prepare("UPDATE models SET material_id = ? WHERE id = ?");
        ASSERT_TRUE(stmt.isValid());
        ASSERT_TRUE(stmt.bindInt(1, *matId));
        ASSERT_TRUE(stmt.bindInt(2, modelId));
        ASSERT_TRUE(stmt.execute());
    }

    // 5. Create project and link model
    dw::ProjectRepository projRepo(m_db);
    dw::ProjectRecord projRec;
    projRec.name = "Material Thumbnail Project";
    projRec.description = "Test";
    auto projId = projRepo.insert(projRec);
    ASSERT_TRUE(projId.has_value());
    ASSERT_TRUE(projRepo.addModel(*projId, modelId));

    auto project = std::make_shared<dw::Project>();
    project->record().id = *projId;
    project->record().name = "Material Thumbnail Project";
    project->addModel(modelId);

    // 6. Export
    dw::ProjectExportManager exporter(m_db);
    auto exportResult = exporter.exportProject(*project, m_archivePath);
    ASSERT_TRUE(exportResult.success) << exportResult.error;

    // 7. Verify ZIP contents
    {
        mz_zip_archive zip{};
        ASSERT_TRUE(mz_zip_reader_init_file(&zip, m_archivePath.c_str(), 0));

        // Check materials/<matId>.dwmat exists
        std::string matArchPath = "materials/" + std::to_string(*matId) + ".dwmat";
        int matIdx = mz_zip_reader_locate_file(&zip, matArchPath.c_str(), nullptr, 0);
        EXPECT_GE(matIdx, 0) << "Material archive entry not found: " << matArchPath;

        // Check thumbnails/<hash>.png exists
        std::string thumbArchPath = "thumbnails/" + modelHash + ".png";
        int thumbIdx = mz_zip_reader_locate_file(&zip, thumbArchPath.c_str(), nullptr, 0);
        EXPECT_GE(thumbIdx, 0) << "Thumbnail archive entry not found: " << thumbArchPath;

        // Check manifest has the new fields
        size_t manifestSize = 0;
        void* manifestData =
            mz_zip_reader_extract_file_to_heap(&zip, "manifest.json", &manifestSize, 0);
        ASSERT_NE(manifestData, nullptr);

        std::string manifestStr(static_cast<const char*>(manifestData), manifestSize);
        mz_free(manifestData);

        auto j = nlohmann::json::parse(manifestStr);
        auto& models = j["models"];
        ASSERT_EQ(models.size(), 1u);

        auto& mj = models[0];
        EXPECT_EQ(mj["material_id"].get<dw::i64>(), *matId);
        EXPECT_FALSE(mj["material_in_archive"].get<std::string>().empty());
        EXPECT_FALSE(mj["thumbnail_in_archive"].get<std::string>().empty());

        mz_zip_reader_end(&zip);
    }

    // 8. Import into a second database
    dw::Database db2;
    ASSERT_TRUE(db2.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db2));

    dw::ProjectExportManager importer(db2);
    auto importResult = importer.importProject(m_archivePath);
    ASSERT_TRUE(importResult.success) << importResult.error;

    // 9. Verify imported model has thumbnail
    dw::ModelRepository modelRepo2(db2);
    auto importedModel = modelRepo2.findByHash(modelHash);
    ASSERT_TRUE(importedModel.has_value());
    EXPECT_FALSE(importedModel->thumbnailPath.empty());
    EXPECT_TRUE(dw::file::exists(importedModel->thumbnailPath));

    // 10. Verify imported model has material_id set
    {
        auto stmt = db2.prepare("SELECT material_id FROM models WHERE id = ?");
        ASSERT_TRUE(stmt.isValid());
        ASSERT_TRUE(stmt.bindInt(1, importedModel->id));
        ASSERT_TRUE(stmt.step());
        EXPECT_FALSE(stmt.isNull(0)) << "Imported model should have material_id set";
    }

    // 11. Verify a MaterialRecord exists in the second DB
    dw::MaterialRepository matRepo2(db2);
    auto allMaterials = matRepo2.findAll();
    EXPECT_GE(allMaterials.size(), 1u) << "Should have at least one imported material";
}

// --- Test 6: Round-trip with all asset types (models, gcode, costs, cut plans, notes) ---

TEST_F(ProjectExportTest, RoundTripAllAssetTypes) {
    // 1. Create project with notes
    dw::ProjectRepository projRepo(m_db);
    dw::ProjectRecord projRec;
    projRec.name = "Full Asset Project";
    projRec.description = "Round trip test";
    projRec.notes = "These are project notes for the round-trip test.";
    auto projId = projRepo.insert(projRec);
    ASSERT_TRUE(projId.has_value());

    // 2. Insert a model + link
    auto modelId = insertModelWithFile("roundtrip_hash_001", "RoundTrip Model");
    ASSERT_TRUE(projRepo.addModel(*projId, modelId));

    // 3. Create a gcode file on disk and insert record
    dw::Path gcodeDir = m_baseDir / "gcode";
    std::filesystem::create_directories(gcodeDir);
    dw::Path gcodeFilePath = gcodeDir / "test_toolpath.nc";
    std::string gcodeContent = "G0 X0 Y0\nG1 X10 Y10 F500\nM30\n";
    ASSERT_TRUE(dw::file::writeText(gcodeFilePath, gcodeContent));

    dw::GCodeRepository gcodeRepo(m_db);
    dw::GCodeRecord gcodeRec;
    gcodeRec.name = "test_toolpath.nc";
    gcodeRec.hash = "gcode_hash_abc123";
    gcodeRec.filePath = gcodeFilePath;
    gcodeRec.fileSize = gcodeContent.size();
    gcodeRec.estimatedTime = 120.5f;
    gcodeRec.toolNumbers = {1, 3};
    auto gcodeId = gcodeRepo.insert(gcodeRec);
    ASSERT_TRUE(gcodeId.has_value());
    ASSERT_TRUE(gcodeRepo.addToProject(*projId, *gcodeId));

    // 4. Insert costing record
    dw::CostRepository costRepo(m_db);
    dw::CostingRecord costRec;
    costRec.name = "Material Cost";
    costRec.projectId = *projId;
    costRec.items.push_back({0, "Plywood Sheet", dw::CostCategory::Material, 2.0, 45.0, 90.0, ""});
    costRec.items.push_back({0, "Router Bit", dw::CostCategory::Tool, 1.0, 25.0, 25.0, ""});
    costRec.recalculate();
    auto costId = costRepo.insert(costRec);
    ASSERT_TRUE(costId.has_value());

    // 5. Insert cut plan
    dw::CutPlanRepository cutPlanRepo(m_db);
    dw::CutPlanRecord cutPlan;
    cutPlan.name = "Main Cut Plan";
    cutPlan.projectId = *projId;
    cutPlan.algorithm = "guillotine";
    cutPlan.allowRotation = true;
    cutPlan.kerf = 3.2f;
    cutPlan.margin = 5.0f;
    cutPlan.sheetsUsed = 2;
    cutPlan.efficiency = 0.85f;
    cutPlan.sheetConfigJson = R"({"width":2440,"height":1220})";
    cutPlan.partsJson = "[]";
    cutPlan.resultJson = "{}";
    auto cutPlanId = cutPlanRepo.insert(cutPlan);
    ASSERT_TRUE(cutPlanId.has_value());

    // 6. Build Project object for export
    auto project = std::make_shared<dw::Project>();
    project->record().id = *projId;
    project->record().name = projRec.name;
    project->record().notes = projRec.notes;
    project->addModel(modelId);

    // 7. Export
    dw::ProjectExportManager exporter(m_db);
    auto exportResult = exporter.exportProject(*project, m_archivePath);
    ASSERT_TRUE(exportResult.success) << exportResult.error;

    // 8. Import into fresh DB
    dw::Database db2;
    ASSERT_TRUE(db2.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db2));

    dw::ProjectExportManager importer(db2);
    auto importResult = importer.importProject(m_archivePath);
    ASSERT_TRUE(importResult.success) << importResult.error;
    ASSERT_TRUE(importResult.importedProjectId.has_value());

    auto importedProjId = *importResult.importedProjectId;

    // 9. Verify project metadata
    dw::ProjectRepository projRepo2(db2);
    auto importedProj = projRepo2.findById(importedProjId);
    ASSERT_TRUE(importedProj.has_value());
    EXPECT_EQ(importedProj->name, "Full Asset Project");
    EXPECT_EQ(importedProj->notes, "These are project notes for the round-trip test.");

    // 10. Verify models
    auto importedModelIds = projRepo2.getModelIds(importedProjId);
    EXPECT_EQ(importedModelIds.size(), 1u);

    // 11. Verify gcode
    dw::GCodeRepository gcodeRepo2(db2);
    auto importedGcodes = gcodeRepo2.findByProject(importedProjId);
    ASSERT_EQ(importedGcodes.size(), 1u);
    EXPECT_EQ(importedGcodes[0].name, "test_toolpath.nc");
    EXPECT_EQ(importedGcodes[0].hash, "gcode_hash_abc123");
    EXPECT_FLOAT_EQ(importedGcodes[0].estimatedTime, 120.5f);
    EXPECT_EQ(importedGcodes[0].toolNumbers.size(), 2u);
    if (importedGcodes[0].toolNumbers.size() == 2) {
        EXPECT_EQ(importedGcodes[0].toolNumbers[0], 1);
        EXPECT_EQ(importedGcodes[0].toolNumbers[1], 3);
    }

    // 12. Verify cost estimates
    dw::CostRepository costRepo2(db2);
    auto importedCosts = costRepo2.findByProject(importedProjId);
    ASSERT_EQ(importedCosts.size(), 1u);
    EXPECT_EQ(importedCosts[0].name, "Material Cost");
    ASSERT_EQ(importedCosts[0].items.size(), 2u);
    EXPECT_EQ(importedCosts[0].items[0].name, "Plywood Sheet");
    EXPECT_EQ(importedCosts[0].items[1].name, "Router Bit");
    EXPECT_DOUBLE_EQ(importedCosts[0].items[0].quantity, 2.0);
    EXPECT_DOUBLE_EQ(importedCosts[0].items[0].rate, 45.0);

    // 13. Verify cut plans
    dw::CutPlanRepository cutPlanRepo2(db2);
    auto importedPlans = cutPlanRepo2.findByProject(importedProjId);
    ASSERT_EQ(importedPlans.size(), 1u);
    EXPECT_EQ(importedPlans[0].name, "Main Cut Plan");
    EXPECT_EQ(importedPlans[0].algorithm, "guillotine");
    EXPECT_EQ(importedPlans[0].sheetsUsed, 2);
    EXPECT_FLOAT_EQ(importedPlans[0].efficiency, 0.85f);
    EXPECT_FLOAT_EQ(importedPlans[0].kerf, 3.2f);
    EXPECT_FALSE(importedPlans[0].sheetConfigJson.empty());
}
