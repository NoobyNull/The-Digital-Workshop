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

    mz_zip_reader_end(&zip);

    log::infof(kLogModuleImport,
               "Imported project '%s' with %d models, %zu gcode from '%s'",
               manifest.projectName.c_str(),
               importedCount,
               manifest.gcode.size(),
               archivePath.string().c_str());

    auto result = DwprojExportResult::ok(importedCount, totalBytes);
    result.importedProjectId = *projectId;
    return result;
}

} // namespace dw
