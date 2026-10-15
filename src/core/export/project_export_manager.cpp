// Digital Workshop - Project Export Manager
// Exports projects as .dwproj ZIP archives (manifest.json + model blobs)
// Import logic is in project_import.cpp

#include "project_export_manager.h"

#include "../materials/material_archive.h"
#include "../paths/app_paths.h"
#include "../paths/path_resolver.h"
#include "../project/project.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

#include <chrono>
#include <cstring>
#include <miniz.h>
#include <nlohmann/json.hpp>

namespace dw {

static constexpr const char* kAppVersion = "1.1.0";
static constexpr const char* kLogModule = "ProjectExport";

// --- Helpers ---

static std::string isoTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

static std::string getBasename(const Path& p) {
    return p.filename().string();
}

static std::string getFileExtension(const Path& p) {
    return p.extension().string();
}

// --- Construction ---

ProjectExportManager::ProjectExportManager(Database& db) : m_db(db) {}

// --- Helpers ---

std::optional<i64> ProjectExportManager::getModelMaterialId(i64 modelId) {
    auto stmt = m_db.prepare("SELECT material_id FROM models WHERE id = ?");
    if (!stmt.isValid() || !stmt.bindInt(1, modelId)) {
        return std::nullopt;
    }
    if (stmt.step()) {
        if (stmt.isNull(0)) {
            return std::nullopt;
        }
        return stmt.getInt(0);
    }
    return std::nullopt;
}

// --- Export ---

DwprojExportResult ProjectExportManager::exportProject(const Project& project,
                                                       const Path& outputPath,
                                                       ExportProgressCallback progress) {
    ModelRepository modelRepo(m_db);
    ProjectRepository projectRepo(m_db);

    // Get model IDs for this project
    auto modelIds = projectRepo.getModelIds(project.id());
    if (modelIds.empty()) {
        return DwprojExportResult::fail("Project has no models to export");
    }

    // Collect model records
    std::vector<ModelRecord> models;
    models.reserve(modelIds.size());
    for (auto id : modelIds) {
        auto rec = modelRepo.findById(id);
        if (rec) {
            models.push_back(std::move(*rec));
        }
    }

    if (models.empty()) {
        return DwprojExportResult::fail("No valid models found in project");
    }

    MaterialRepository materialRepo(m_db);

    // Initialize ZIP writer
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, outputPath.string().c_str(), 0)) {
        return DwprojExportResult::fail("Failed to create archive file: " + outputPath.string());
    }

    // Add each model blob
    uint64_t totalBytes = 0;
    int itemIndex = 0;
    int totalItems = static_cast<int>(models.size());

    for (const auto& model : models) {
        auto blobData = file::readBinary(PathResolver::resolve(model.filePath, PathCategory::Support));
        if (!blobData) {
            log::warningf(kLogModule,
                          "Skipping model '%s': cannot read file '%s'",
                          model.name.c_str(),
                          model.filePath.string().c_str());
            itemIndex++;
            continue;
        }

        std::string ext = getFileExtension(model.filePath);
        std::string archPath = "models/" + model.hash + ext;

        if (!mz_zip_writer_add_mem(&zip,
                                   archPath.c_str(),
                                   blobData->data(),
                                   blobData->size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            (void)file::remove(outputPath);
            return DwprojExportResult::fail("Failed to add model blob: " + model.name);
        }

        totalBytes += blobData->size();
        itemIndex++;

        if (progress) {
            progress(itemIndex, totalItems, model.name);
        }
    }

    // Phase A: Export thumbnails
    std::unordered_map<std::string, std::string> hashToThumbnailPath;
    for (const auto& model : models) {
        if (model.thumbnailPath.empty() || !file::exists(model.thumbnailPath)) {
            continue;
        }
        auto thumbData = file::readBinary(model.thumbnailPath);
        if (!thumbData) {
            continue;
        }
        std::string thumbArchPath = "thumbnails/" + model.hash + ".png";
        if (!mz_zip_writer_add_mem(&zip,
                                   thumbArchPath.c_str(),
                                   thumbData->data(),
                                   thumbData->size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            log::warningf(kLogModule, "Failed to add thumbnail for model '%s'", model.name.c_str());
            continue;
        }
        hashToThumbnailPath[model.hash] = thumbArchPath;
    }

    // Phase B: Export materials
    std::unordered_map<i64, i64> modelIdToMaterialId;
    std::unordered_set<i64> writtenMaterialIds;
    for (const auto& model : models) {
        auto matId = getModelMaterialId(model.id);
        if (!matId) {
            continue;
        }
        modelIdToMaterialId[model.id] = *matId;

        if (writtenMaterialIds.count(*matId)) {
            continue; // Already written (shared material)
        }

        auto matRec = materialRepo.findById(*matId);
        if (!matRec || matRec->archivePath.empty() || !file::exists(matRec->archivePath)) {
            continue;
        }

        auto matData = file::readBinary(matRec->archivePath);
        if (!matData) {
            continue;
        }

        std::string matArchPath = "materials/" + std::to_string(*matId) + ".dwmat";
        if (!mz_zip_writer_add_mem(&zip,
                                   matArchPath.c_str(),
                                   matData->data(),
                                   matData->size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            log::warningf(kLogModule,
                          "Failed to add material %lld for model '%s'",
                          static_cast<long long>(*matId),
                          model.name.c_str());
            continue;
        }
        writtenMaterialIds.insert(*matId);
    }

    // Phase C: Export G-code files
    GCodeRepository gcodeRepo(m_db);
    auto gcodeFiles = gcodeRepo.findByProject(project.id());
    std::vector<ManifestGCode> gcodeEntries;
    for (const auto& gc : gcodeFiles) {
        Path resolvedGcPath = PathResolver::resolve(gc.filePath, PathCategory::GCode);
        std::string archivePath =
            "gcode/" + std::to_string(gc.id) + getFileExtension(resolvedGcPath);
        auto fileData = file::readBinary(resolvedGcPath);
        if (!fileData) {
            log::warningf(
                kLogModule, "Skipping gcode '%s': cannot read file", gc.name.c_str());
            continue;
        }
        if (!mz_zip_writer_add_mem(&zip,
                                   archivePath.c_str(),
                                   fileData->data(),
                                   fileData->size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            log::warningf(kLogModule, "Failed to add gcode '%s'", gc.name.c_str());
            continue;
        }
        totalBytes += fileData->size();

        ManifestGCode entry;
        entry.id = gc.id;
        entry.name = gc.name;
        entry.hash = gc.hash;
        entry.fileInArchive = archivePath;
        entry.estimatedTime = gc.estimatedTime;
        entry.toolNumbers = gc.toolNumbers;
        gcodeEntries.push_back(std::move(entry));
    }

    // Get project notes
    std::string projectNotes = project.record().notes;

    // Build manifest with all data and add to ZIP
    std::string manifestJson = buildManifestJson(project,
                                                 models,
                                                 modelIdToMaterialId,
                                                 hashToThumbnailPath,
                                                 gcodeEntries,
                                                 projectNotes);

    if (!mz_zip_writer_add_mem(&zip,
                               "manifest.json",
                               manifestJson.data(),
                               manifestJson.size(),
                               MZ_DEFAULT_COMPRESSION)) {
        mz_zip_writer_end(&zip);
        (void)file::remove(outputPath);
        return DwprojExportResult::fail("Failed to write manifest to archive");
    }

    // Finalize archive
    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        (void)file::remove(outputPath);
        return DwprojExportResult::fail("Failed to finalize archive");
    }

    mz_zip_writer_end(&zip);

    int modelCount = static_cast<int>(models.size());
    log::infof(kLogModule,
               "Exported project '%s' with %d models, %d gcode (%llu bytes) to '%s'",
               project.name().c_str(),
               modelCount,
               static_cast<int>(gcodeEntries.size()),
               static_cast<unsigned long long>(totalBytes),
               outputPath.string().c_str());

    return DwprojExportResult::ok(modelCount, totalBytes);
}

// --- Manifest JSON ---

std::string ProjectExportManager::buildManifestJson(
    const Project& project,
    const std::vector<ModelRecord>& models,
    const std::unordered_map<i64, i64>& modelIdToMaterialId,
    const std::unordered_map<std::string, std::string>& hashToThumbnailPath,
    const std::vector<ManifestGCode>& gcodeEntries,
    const std::string& projectNotes) {
    nlohmann::json j;
    j["format_version"] = FormatVersion;
    j["app_version"] = kAppVersion;
    j["created_at"] = isoTimestamp();
    j["project_id"] = project.id();
    j["project_name"] = project.name();
    j["project_notes"] = projectNotes;

    nlohmann::json modelsArr = nlohmann::json::array();
    for (const auto& m : models) {
        std::string ext = getFileExtension(m.filePath);
        std::string archPath = "models/" + m.hash + ext;

        nlohmann::json mj;
        mj["name"] = m.name;
        mj["hash"] = m.hash;
        mj["original_filename"] = getBasename(m.filePath);
        mj["file_in_archive"] = archPath;
        mj["file_format"] = m.fileFormat;
        mj["tags"] = m.tags;
        mj["vertex_count"] = m.vertexCount;
        mj["triangle_count"] = m.triangleCount;
        mj["bounds_min"] = {m.boundsMin.x, m.boundsMin.y, m.boundsMin.z};
        mj["bounds_max"] = {m.boundsMax.x, m.boundsMax.y, m.boundsMax.z};

        // Material info
        auto matIt = modelIdToMaterialId.find(m.id);
        if (matIt != modelIdToMaterialId.end()) {
            mj["material_id"] = matIt->second;
            mj["material_in_archive"] = "materials/" + std::to_string(matIt->second) + ".dwmat";
        } else {
            mj["material_id"] = nullptr;
            mj["material_in_archive"] = "";
        }

        // Thumbnail info
        auto thumbIt = hashToThumbnailPath.find(m.hash);
        if (thumbIt != hashToThumbnailPath.end()) {
            mj["thumbnail_in_archive"] = thumbIt->second;
        } else {
            mj["thumbnail_in_archive"] = "";
        }

        modelsArr.push_back(mj);
    }
    j["models"] = modelsArr;

    // G-code entries
    nlohmann::json gcodeArr = nlohmann::json::array();
    for (const auto& gc : gcodeEntries) {
        nlohmann::json gj;
        gj["id"] = gc.id;
        gj["name"] = gc.name;
        gj["hash"] = gc.hash;
        gj["file_in_archive"] = gc.fileInArchive;
        gj["estimated_time"] = gc.estimatedTime;
        gj["tool_numbers"] = gc.toolNumbers;
        gcodeArr.push_back(gj);
    }
    j["gcode"] = gcodeArr;

    return j.dump(2);
}

bool ProjectExportManager::parseManifest(const std::string& json,
                                         Manifest& out,
                                         std::string& error) {
    try {
        auto j = nlohmann::json::parse(json);

        // Required fields
        if (!j.contains("format_version") || !j.contains("models")) {
            error = "Missing required fields (format_version, models)";
            return false;
        }

        out.formatVersion = j.value("format_version", 1);
        out.appVersion = j.value("app_version", "unknown");
        out.createdAt = j.value("created_at", "");
        out.projectId = j.value("project_id", static_cast<i64>(0));
        out.projectName = j.value("project_name", "Imported Project");

        out.projectNotes = j.value("project_notes", "");

        // Parse models array -- unknown fields are silently ignored
        for (const auto& mj : j["models"]) {
            ManifestModel mm;
            mm.name = mj.value("name", "Unnamed");
            mm.hash = mj.value("hash", "");
            mm.originalFilename = mj.value("original_filename", "");
            mm.fileInArchive = mj.value("file_in_archive", "");
            mm.fileFormat = mj.value("file_format", "stl");
            mm.vertexCount = mj.value("vertex_count", static_cast<u32>(0));
            mm.triangleCount = mj.value("triangle_count", static_cast<u32>(0));

            if (mj.contains("tags") && mj["tags"].is_array()) {
                for (const auto& t : mj["tags"]) {
                    mm.tags.push_back(t.get<std::string>());
                }
            }

            if (mj.contains("bounds_min") && mj["bounds_min"].is_array() &&
                mj["bounds_min"].size() == 3) {
                mm.boundsMin = Vec3(mj["bounds_min"][0].get<float>(),
                                    mj["bounds_min"][1].get<float>(),
                                    mj["bounds_min"][2].get<float>());
            }
            if (mj.contains("bounds_max") && mj["bounds_max"].is_array() &&
                mj["bounds_max"].size() == 3) {
                mm.boundsMax = Vec3(mj["bounds_max"][0].get<float>(),
                                    mj["bounds_max"][1].get<float>(),
                                    mj["bounds_max"][2].get<float>());
            }

            // Material and thumbnail fields (optional, for forward compat)
            if (mj.contains("material_id") && !mj["material_id"].is_null()) {
                mm.materialId = mj["material_id"].get<i64>();
            }
            mm.materialInArchive = mj.value("material_in_archive", "");
            mm.thumbnailInArchive = mj.value("thumbnail_in_archive", "");

            if (mm.hash.empty()) {
                log::warningf(kLogModule, "Skipping model with missing hash in manifest");
                continue;
            }

            out.models.push_back(std::move(mm));
        }

        // Parse gcode array
        if (j.contains("gcode") && j["gcode"].is_array()) {
            for (const auto& gj : j["gcode"]) {
                ManifestGCode gc;
                gc.id = gj.value("id", static_cast<i64>(0));
                gc.name = gj.value("name", "");
                gc.hash = gj.value("hash", "");
                gc.fileInArchive = gj.value("file_in_archive", "");
                gc.estimatedTime = gj.value("estimated_time", 0.0f);
                if (gj.contains("tool_numbers") && gj["tool_numbers"].is_array()) {
                    for (const auto& tn : gj["tool_numbers"]) {
                        gc.toolNumbers.push_back(tn.get<int>());
                    }
                }
                out.gcode.push_back(std::move(gc));
            }
        }

        return true;
    } catch (const nlohmann::json::exception& e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

} // namespace dw
