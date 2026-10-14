#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../database/gcode_repository.h"
#include "../database/material_repository.h"
#include "../database/model_repository.h"
#include "../database/project_repository.h"
#include "../types.h"

namespace dw {

// Forward declarations
class Database;
class Project;

// Result of export/import operations
struct DwprojExportResult {
    bool success = false;
    std::string error;
    int modelCount = 0;
    uint64_t totalBytes = 0;
    std::optional<i64> importedProjectId; // Set on successful import, nullopt on export or failure

    static DwprojExportResult ok(int models = 0, uint64_t bytes = 0) {
        return {true, "", models, bytes, std::nullopt};
    }

    static DwprojExportResult fail(const std::string& err) {
        return {false, err, 0, 0, std::nullopt};
    }
};

// Progress callback: (current, total, currentItemName)
using ExportProgressCallback = std::function<void(int, int, const std::string&)>;

// Manages exporting/importing projects as .dwproj ZIP archives.
// A .dwproj archive contains:
//   - manifest.json  (project metadata + model list)
//   - models/<hash>.<ext>  (model blob files)
class ProjectExportManager {
  public:
    explicit ProjectExportManager(Database& db);

    // Export a project and its models to a .dwproj ZIP at outputPath
    DwprojExportResult exportProject(const Project& project,
                                     const Path& outputPath,
                                     ExportProgressCallback progress = nullptr);

    // Import a .dwproj ZIP, creating project + models in DB
    DwprojExportResult importProject(const Path& archivePath,
                                     ExportProgressCallback progress = nullptr);

    static constexpr const char* Extension = ".dwproj";
    static constexpr int FormatVersion = 2;

  private:
    struct ManifestModel {
        std::string name;
        std::string hash;
        std::string originalFilename;
        std::string fileInArchive;
        std::string fileFormat;
        std::vector<std::string> tags;
        u32 vertexCount = 0;
        u32 triangleCount = 0;
        Vec3 boundsMin{0.0f};
        Vec3 boundsMax{0.0f};
        std::optional<i64> materialId;  // material_id from models table
        std::string materialInArchive;  // e.g. "materials/3.dwmat"
        std::string thumbnailInArchive; // e.g. "thumbnails/<hash>.png"
    };

    struct ManifestGCode {
        i64 id = 0;
        std::string name;
        std::string hash;
        std::string fileInArchive; // "gcode/1.nc"
        f32 estimatedTime = 0.0f;
        std::vector<int> toolNumbers;
    };

    struct Manifest {
        int formatVersion = 2;
        std::string appVersion;
        std::string createdAt;
        i64 projectId = 0;
        std::string projectName;
        std::string projectNotes;
        std::vector<ManifestModel> models;
        std::vector<ManifestGCode> gcode;
    };

    std::string buildManifestJson(
        const Project& project,
        const std::vector<ModelRecord>& models,
        const std::unordered_map<i64, i64>& modelIdToMaterialId,
        const std::unordered_map<std::string, std::string>& hashToThumbnailPath,
        const std::vector<ManifestGCode>& gcodeEntries,
        const std::string& projectNotes);
    bool parseManifest(const std::string& json, Manifest& out, std::string& error);
    std::optional<i64> getModelMaterialId(i64 modelId);

    Database& m_db;
};

} // namespace dw
