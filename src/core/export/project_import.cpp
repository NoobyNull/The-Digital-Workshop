// Digital Workshop - Project Import
// Import logic for .dwproj ZIP archives (split from project_export_manager.cpp)

#include "project_export_manager.h"

#include "../config/config.h"
#include "../materials/material_archive.h"
#include "../paths/app_paths.h"
#include "../paths/path_resolver.h"
#include "../project/project.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

#include <cstring>
#include <miniz.h>
#include <nlohmann/json.hpp>

namespace dw {

static constexpr const char* kManifestFileImport = "manifest.json";
static constexpr const char* kLogModuleImport = "ProjectImport";

static bool containsPathTraversalImport(const std::string& path) {
    return path.find("..") != std::string::npos;
}

// --- Import ---

DwprojExportResult ProjectExportManager::importProject(const Path& archivePath,
                                                       ExportProgressCallback progress) {
    ModelRepository modelRepo(m_db);
    ProjectRepository projectRepo(m_db);

    // Open ZIP reader
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivePath.string().c_str(), 0)) {
        return DwprojExportResult::fail("Failed to open archive: " + archivePath.string());
    }

    // Extract manifest.json
    size_t manifestSize = 0;
    void* manifestData =
        mz_zip_reader_extract_file_to_heap(&zip, kManifestFileImport, &manifestSize, 0);
    if (!manifestData) {
        mz_zip_reader_end(&zip);
        return DwprojExportResult::fail("Archive missing manifest.json");
    }

    std::string manifestJson(static_cast<const char*>(manifestData), manifestSize);
    mz_free(manifestData);

    // Parse manifest
    Manifest manifest;
    std::string parseError;
    if (!parseManifest(manifestJson, manifest, parseError)) {
        mz_zip_reader_end(&zip);
        return DwprojExportResult::fail("Invalid manifest: " + parseError);
    }

    // Warn on newer format version (but continue for forward compat)
    if (manifest.formatVersion > FormatVersion) {
        log::warningf(kLogModuleImport,
                      "Archive format version %d is newer than supported version %d. "
                      "Some features may be unavailable.",
                      manifest.formatVersion,
                      FormatVersion);
    }

    // Create project record
    ProjectRecord projRec;
    projRec.name = manifest.projectName;
    projRec.notes = manifest.projectNotes;
    projRec.description = "Imported from " + archivePath.filename().string();

    auto projectId = projectRepo.insert(projRec);
    if (!projectId) {
        mz_zip_reader_end(&zip);
        return DwprojExportResult::fail("Failed to create project in database");
    }

    // Ensure models directory exists
    Path modelsDir = Config::instance().getModelsDir();
    (void)file::createDirectories(modelsDir);

    // Import each model
    int total = static_cast<int>(manifest.models.size() + manifest.gcode.size());
    int current = 0;
    int importedCount = 0;
    uint64_t totalBytes = 0;

    for (size_t i = 0; i < manifest.models.size(); i++) {
        const auto& mm = manifest.models[i];

        // Path traversal check
        if (containsPathTraversalImport(mm.fileInArchive)) {
            log::warningf(kLogModuleImport,
                          "Skipping model with suspicious path: %s",
                          mm.fileInArchive.c_str());
            continue;
        }

        // Check if model already exists (dedup by hash)
        bool alreadyExists = modelRepo.exists(mm.hash);

        if (!alreadyExists) {
            // Extract blob from ZIP
            size_t blobSize = 0;
            void* blobData =
                mz_zip_reader_extract_file_to_heap(&zip, mm.fileInArchive.c_str(), &blobSize, 0);
            if (!blobData) {
                log::warningf(kLogModuleImport,
                              "Failed to extract model blob: %s",
                              mm.fileInArchive.c_str());
                continue;
            }

            // Determine file extension from archive path
            Path archPath(mm.fileInArchive);
            std::string ext = archPath.extension().string();
            if (ext.empty()) {
                ext = "." + mm.fileFormat;
            }

            // Write blob to models directory
            Path destPath = modelsDir / (mm.hash + ext);
            if (!file::writeBinary(destPath, blobData, blobSize)) {
                mz_free(blobData);
                log::warningf(kLogModuleImport,
                              "Failed to write model blob to: %s",
                              destPath.string().c_str());
                continue;
            }

            totalBytes += blobSize;
            mz_free(blobData);

            // Create model record
            ModelRecord rec;
            rec.hash = mm.hash;
            rec.name = mm.name;
            rec.filePath = PathResolver::makeStorable(destPath, PathCategory::Models);
            rec.fileFormat = mm.fileFormat;
            rec.fileSize = blobSize;
            rec.vertexCount = mm.vertexCount;
            rec.triangleCount = mm.triangleCount;
            rec.boundsMin = mm.boundsMin;
            rec.boundsMax = mm.boundsMax;
            rec.tags = mm.tags;

            auto modelId = modelRepo.insert(rec);
            if (modelId) {
                (void)projectRepo.addModel(*projectId, *modelId, static_cast<int>(i));
            }
        } else {
            // Model already exists -- just link to project
            auto existing = modelRepo.findByHash(mm.hash);
            if (existing) {
                (void)projectRepo.addModel(*projectId, existing->id, static_cast<int>(i));
            }
        }

        importedCount++;
        current++;

        if (progress) {
            progress(current, total, mm.name);
        }
    }

    // Phase A: Restore thumbnails
    Path thumbnailDir = paths::getThumbnailDir();
    (void)file::createDirectories(thumbnailDir);

    for (size_t i = 0; i < manifest.models.size(); i++) {
        const auto& mm = manifest.models[i];
        if (mm.thumbnailInArchive.empty() ||
            containsPathTraversalImport(mm.thumbnailInArchive)) {
            continue;
        }

        size_t thumbSize = 0;
        void* thumbData =
            mz_zip_reader_extract_file_to_heap(&zip, mm.thumbnailInArchive.c_str(), &thumbSize, 0);
        if (!thumbData) {
            continue;
        }

        Path thumbDest = thumbnailDir / (mm.hash + ".png");
        if (file::writeBinary(thumbDest, thumbData, thumbSize)) {
            // Find the model we just imported and update its thumbnail
            auto imported = modelRepo.findByHash(mm.hash);
            if (imported) {
                modelRepo.updateThumbnail(imported->id, thumbDest);
            }
        }
        mz_free(thumbData);
    }

    // Phase B: Restore materials
    MaterialRepository materialRepo(m_db);
    Path materialsDir = paths::getMaterialsDir();
    (void)file::createDirectories(materialsDir);

    std::unordered_map<i64, i64> oldToNewMaterialId;
    for (size_t i = 0; i < manifest.models.size(); i++) {
        const auto& mm = manifest.models[i];
        if (!mm.materialId || mm.materialInArchive.empty() ||
            containsPathTraversalImport(mm.materialInArchive)) {
            continue;
        }

        i64 oldMatId = *mm.materialId;

        // Find the imported model
        auto imported = modelRepo.findByHash(mm.hash);
        if (!imported) {
            continue;
        }

        i64 newMatId = 0;
        auto mapIt = oldToNewMaterialId.find(oldMatId);
        if (mapIt != oldToNewMaterialId.end()) {
            newMatId = mapIt->second;
        } else {
            // Extract .dwmat from ZIP
            size_t matSize = 0;
            void* matData = mz_zip_reader_extract_file_to_heap(
                &zip, mm.materialInArchive.c_str(), &matSize, 0);
            if (!matData) {
                continue;
            }

            std::string matFilename = std::to_string(oldMatId) + ".dwmat";
            Path matDest = materialsDir / matFilename;
            if (!file::writeBinary(matDest, matData, matSize)) {
                mz_free(matData);
                continue;
            }
            mz_free(matData);

            // Try to load metadata from the .dwmat archive
            MaterialRecord matRec;
            auto matArchiveData = MaterialArchive::load(matDest.string());
            if (matArchiveData) {
                matRec = matArchiveData->metadata;
            } else {
                matRec.name = "Imported Material " + std::to_string(oldMatId);
            }
            matRec.archivePath = matDest;

            auto insertedId = materialRepo.insert(matRec);
            if (!insertedId) {
                continue;
            }
            newMatId = *insertedId;
            oldToNewMaterialId[oldMatId] = newMatId;
        }

        // Assign material to model
        auto stmt = m_db.prepare("UPDATE models SET material_id = ? WHERE id = ?");
        if (stmt.isValid() && stmt.bindInt(1, newMatId) && stmt.bindInt(2, imported->id)) {
            (void)stmt.execute();
        }
    }

    // Phase C: Import G-code files
    GCodeRepository gcodeRepo(m_db);
    Path gcodeDir = Config::instance().getGCodeDir();
    (void)file::createDirectories(gcodeDir);

    for (const auto& gc : manifest.gcode) {
        if (gc.fileInArchive.empty() || containsPathTraversalImport(gc.fileInArchive)) {
            current++;
            continue;
        }

        // Extract file from ZIP
        size_t fileSize = 0;
        void* fileData =
            mz_zip_reader_extract_file_to_heap(&zip, gc.fileInArchive.c_str(), &fileSize, 0);
        if (!fileData) {
            log::warningf(
                kLogModuleImport, "Failed to extract gcode: %s", gc.fileInArchive.c_str());
            current++;
            continue;
        }

        // Write to gcode storage directory
        Path archPath(gc.fileInArchive);
        std::string ext = archPath.extension().string();
        if (ext.empty()) ext = ".nc";
        Path outPath = gcodeDir / (gc.hash + ext);
        if (!file::writeBinary(outPath, fileData, fileSize)) {
            mz_free(fileData);
            current++;
            continue;
        }
        totalBytes += fileSize;
        mz_free(fileData);

        // Check if gcode with this hash already exists (dedup)
        auto existing = gcodeRepo.findByHash(gc.hash);
        i64 gcodeId;
        if (existing) {
            gcodeId = existing->id;
        } else {
            GCodeRecord record;
            record.hash = gc.hash;
            record.name = gc.name;
            record.filePath = PathResolver::makeStorable(outPath, PathCategory::GCode);
            record.fileSize = fileSize;
            record.estimatedTime = gc.estimatedTime;
            record.toolNumbers = gc.toolNumbers;
            auto id = gcodeRepo.insert(record);
            if (!id) {
                current++;
                continue;
            }
            gcodeId = *id;
        }

        gcodeRepo.addToProject(*projectId, gcodeId, static_cast<int>(current));
        current++;

        if (progress) {
            progress(current, total, gc.name);
        }
    }

    // Phase D: Import cost estimates
    size_t costsSize = 0;
    void* costsData = mz_zip_reader_extract_file_to_heap(&zip, "costs.json", &costsSize, 0);
    if (costsData) {
        std::string costsJson(static_cast<char*>(costsData), costsSize);
        mz_free(costsData);

        auto estimates = parseCostsJson(costsJson);
        CostRepository costRepo(m_db);
        for (auto& est : estimates) {
            est.projectId = *projectId;
            costRepo.insert(est);
        }
    }

    // Phase E: Import cut plans
    size_t plansSize = 0;
    void* plansData = mz_zip_reader_extract_file_to_heap(&zip, "cut_plans.json", &plansSize, 0);
    if (plansData) {
        std::string plansJson(static_cast<char*>(plansData), plansSize);
        mz_free(plansData);

        auto plans = parseCutPlansJson(plansJson);
        CutPlanRepository cutPlanRepo(m_db);
        for (auto& plan : plans) {
            plan.projectId = *projectId;
            cutPlanRepo.insert(plan);
        }
    }

    mz_zip_reader_end(&zip);

    log::infof(kLogModuleImport,
               "Imported project '%s' with %d models, %zu gcode, costs, cut plans from '%s'",
               manifest.projectName.c_str(),
               importedCount,
               manifest.gcode.size(),
               archivePath.string().c_str());

    auto result = DwprojExportResult::ok(importedCount, totalBytes);
    result.importedProjectId = *projectId;
    return result;
}

// --- JSON Parsing Helpers ---

std::vector<CostingRecord> ProjectExportManager::parseCostsJson(const std::string& json) {
    std::vector<CostingRecord> results;
    try {
        auto arr = nlohmann::json::parse(json);
        if (!arr.is_array()) return results;

        for (const auto& ej : arr) {
            CostingRecord est;
            est.name = ej.value("name", "");
            est.projectId = ej.value("project_id", static_cast<i64>(0));
            est.subtotal = ej.value("subtotal", 0.0);
            est.taxRate = ej.value("tax_rate", 0.0);
            est.taxAmount = ej.value("tax_amount", 0.0);
            est.discountRate = ej.value("discount_rate", 0.0);
            est.discountAmount = ej.value("discount_amount", 0.0);
            est.total = ej.value("total", 0.0);
            est.notes = ej.value("notes", "");
            est.createdAt = ej.value("created_at", "");
            est.modifiedAt = ej.value("modified_at", "");

            if (ej.contains("items") && ej["items"].is_array()) {
                for (const auto& ij : ej["items"]) {
                    CostItem item;
                    item.name = ij.value("name", "");
                    item.category = static_cast<CostCategory>(ij.value("category", 0));
                    item.quantity = ij.value("quantity", 1.0);
                    item.rate = ij.value("rate", 0.0);
                    item.total = ij.value("total", 0.0);
                    item.notes = ij.value("notes", "");
                    est.items.push_back(std::move(item));
                }
            }

            results.push_back(std::move(est));
        }
    } catch (const nlohmann::json::exception& e) {
        log::warningf(kLogModuleImport, "Failed to parse costs.json: %s", e.what());
    }
    return results;
}

std::vector<CutPlanRecord> ProjectExportManager::parseCutPlansJson(const std::string& json) {
    std::vector<CutPlanRecord> results;
    try {
        auto arr = nlohmann::json::parse(json);
        if (!arr.is_array()) return results;

        for (const auto& pj : arr) {
            CutPlanRecord rec;
            rec.name = pj.value("name", "");
            if (pj.contains("project_id") && !pj["project_id"].is_null()) {
                rec.projectId = pj["project_id"].get<i64>();
            }
            rec.algorithm = pj.value("algorithm", "");
            rec.allowRotation = pj.value("allow_rotation", true);
            rec.kerf = pj.value("kerf", 0.0f);
            rec.margin = pj.value("margin", 0.0f);
            rec.sheetsUsed = pj.value("sheets_used", 0);
            rec.efficiency = pj.value("efficiency", 0.0f);
            rec.createdAt = pj.value("created_at", "");
            rec.modifiedAt = pj.value("modified_at", "");

            // Re-serialize embedded JSON fields
            if (pj.contains("sheet_config") && !pj["sheet_config"].is_null()) {
                rec.sheetConfigJson = pj["sheet_config"].dump();
            }
            if (pj.contains("parts") && !pj["parts"].is_null()) {
                rec.partsJson = pj["parts"].dump();
            }
            if (pj.contains("result") && !pj["result"].is_null()) {
                rec.resultJson = pj["result"].dump();
            }

            results.push_back(std::move(rec));
        }
    } catch (const nlohmann::json::exception& e) {
        log::warningf(kLogModuleImport, "Failed to parse cut_plans.json: %s", e.what());
    }
    return results;
}

} // namespace dw
