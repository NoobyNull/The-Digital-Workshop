#include "project_directory_importer.h"

#include <cmath>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "../config/config.h"
#include "../database/database.h"
#include "../database/gcode_repository.h"
#include "../database/project_repository.h"
#include "../library/library_manager.h"
#include "../loaders/gcode_loader.h"
#include "../mesh/hash.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "project_directory.h"

namespace dw {
namespace {

constexpr int kSupportedManifestVersion = 1;

struct ManifestModel {
    std::string filename;
    std::string hash;
};

struct ManifestGCode {
    std::string filename;
    std::string toolDescription;
};

struct ManifestData {
    Path root;
    std::string name;
    std::string description;
    std::vector<ManifestModel> models;
    std::vector<std::string> heightmaps;
    std::vector<ManifestGCode> gcode;
};

struct PreparedModel {
    Path path;
    std::string hash;
};

struct PreparedGCode {
    Path path;
    std::string hash;
    GCodeRecord record;
};

struct PreparedManifest {
    ManifestData manifest;
    std::vector<PreparedModel> models;
    std::vector<PreparedGCode> gcode;
};

ProjectDirectoryImportResult fail(ProjectDirectoryImportError error, std::string message) {
    ProjectDirectoryImportResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

bool isSafeLeafName(const std::string& filename) {
    if (filename.empty() || filename == "." || filename == ".." ||
        filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos ||
        filename.find(':') != std::string::npos || filename.find('\0') != std::string::npos) {
        return false;
    }
    for (char raw : filename) {
        const auto ch = static_cast<unsigned char>(raw);
        if (ch < 0x20U) {
            return false;
        }
    }
    const Path path(filename);
    return !path.is_absolute() && !path.has_root_path() && path.filename() == path;
}

bool isWithin(const Path& base, const Path& candidate) {
    auto baseIt = base.begin();
    auto candidateIt = candidate.begin();
    for (; baseIt != base.end(); ++baseIt, ++candidateIt) {
        if (candidateIt == candidate.end() || *candidateIt != *baseIt) {
            return false;
        }
    }
    return true;
}

std::optional<Path> validateEntryPath(const Path& directory,
                                      const std::string& filename,
                                      ProjectDirectoryImportResult& error) {
    if (!isSafeLeafName(filename)) {
        error = fail(ProjectDirectoryImportError::UnsafeEntry, "Unsafe project entry: " + filename);
        return std::nullopt;
    }

    const Path candidate = directory / filename;
    if (!file::isFile(candidate)) {
        error = fail(ProjectDirectoryImportError::MissingEntry,
                     "Project entry is missing: " + filename);
        return std::nullopt;
    }

    std::error_code ec;
    const Path canonicalBase = std::filesystem::canonical(directory, ec);
    if (ec) {
        error = fail(ProjectDirectoryImportError::InvalidRoot,
                     "Cannot resolve project content directory: " + directory.string());
        return std::nullopt;
    }
    const Path canonicalCandidate = std::filesystem::canonical(candidate, ec);
    if (ec || !isWithin(canonicalBase, canonicalCandidate)) {
        error = fail(ProjectDirectoryImportError::UnsafeEntry,
                     "Project entry escapes its content directory: " + filename);
        return std::nullopt;
    }
    return canonicalCandidate;
}

std::optional<ManifestData> parseManifest(const ProjectDirectory& directory,
                                          ProjectDirectoryImportResult& error) {
    if (directory.root().empty() || !file::isDirectory(directory.root())) {
        error = fail(ProjectDirectoryImportError::InvalidRoot, "Project directory does not exist");
        return std::nullopt;
    }

    std::error_code ec;
    const Path root = std::filesystem::canonical(directory.root(), ec);
    if (ec) {
        error = fail(ProjectDirectoryImportError::InvalidRoot, "Cannot resolve project directory");
        return std::nullopt;
    }

    const Path manifestPath = root / "project.json";
    auto manifestText = file::readText(manifestPath);
    if (!manifestText) {
        error = fail(ProjectDirectoryImportError::ManifestMissing,
                     "Project folder is missing project.json");
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(*manifestText);
        if (!json.is_object() || !json.contains("version") ||
            !json["version"].is_number_integer() || !json.contains("name") ||
            !json["name"].is_string() || json["name"].get<std::string>().empty()) {
            error = fail(ProjectDirectoryImportError::InvalidManifest,
                         "Project manifest requires an integer version and non-empty name");
            return std::nullopt;
        }
        if (json["version"].get<int>() != kSupportedManifestVersion) {
            error = fail(ProjectDirectoryImportError::UnsupportedManifestVersion,
                         "Unsupported project manifest version");
            return std::nullopt;
        }

        ManifestData manifest;
        manifest.root = root;
        manifest.name = json["name"].get<std::string>();
        if (json.contains("description")) {
            if (!json["description"].is_string()) {
                error = fail(ProjectDirectoryImportError::InvalidManifest,
                             "Project description must be text");
                return std::nullopt;
            }
            manifest.description = json["description"].get<std::string>();
        }

        for (const char* key : {"models", "heightmaps", "gcode"}) {
            if (json.contains(key) && !json[key].is_array()) {
                error = fail(ProjectDirectoryImportError::InvalidManifest,
                             std::string("Project manifest field is not an array: ") + key);
                return std::nullopt;
            }
        }

        if (json.contains("models")) {
            for (const auto& item : json["models"]) {
                if (!item.is_object() || !item.contains("filename") ||
                    !item["filename"].is_string() || !item.contains("hash") ||
                    !item["hash"].is_string() || item["hash"].get<std::string>().empty()) {
                    error = fail(ProjectDirectoryImportError::InvalidManifest,
                                 "Every model entry requires filename and hash text");
                    return std::nullopt;
                }
                manifest.models.push_back(
                    {item["filename"].get<std::string>(), item["hash"].get<std::string>()});
            }
        }

        if (json.contains("heightmaps")) {
            for (const auto& item : json["heightmaps"]) {
                if (!item.is_object() || !item.contains("filename") ||
                    !item["filename"].is_string() || !item.contains("resolutionMmPerPx") ||
                    !item["resolutionMmPerPx"].is_number() ||
                    !std::isfinite(item["resolutionMmPerPx"].get<double>()) ||
                    item["resolutionMmPerPx"].get<double>() <= 0.0) {
                    error = fail(ProjectDirectoryImportError::InvalidManifest,
                                 "Every heightmap requires a filename and positive resolution");
                    return std::nullopt;
                }
                manifest.heightmaps.push_back(item["filename"].get<std::string>());
            }
        }

        if (json.contains("gcode")) {
            for (const auto& item : json["gcode"]) {
                if (!item.is_object() || !item.contains("filename") ||
                    !item["filename"].is_string()) {
                    error = fail(ProjectDirectoryImportError::InvalidManifest,
                                 "Every G-code entry requires a filename");
                    return std::nullopt;
                }
                ManifestGCode entry;
                entry.filename = item["filename"].get<std::string>();
                if (item.contains("toolDescription")) {
                    if (!item["toolDescription"].is_string()) {
                        error = fail(ProjectDirectoryImportError::InvalidManifest,
                                     "G-code tool description must be text");
                        return std::nullopt;
                    }
                    entry.toolDescription = item["toolDescription"].get<std::string>();
                }
                manifest.gcode.push_back(std::move(entry));
            }
        }
        return manifest;
    } catch (const nlohmann::json::exception& exception) {
        error = fail(ProjectDirectoryImportError::InvalidManifest,
                     "Invalid project manifest: " + std::string(exception.what()));
        return std::nullopt;
    }
}

std::optional<PreparedManifest> preflight(const ProjectDirectory& directory,
                                          ProjectDirectoryImportResult& error) {
    auto manifest = parseManifest(directory, error);
    if (!manifest) {
        return std::nullopt;
    }

    PreparedManifest prepared;
    prepared.manifest = std::move(*manifest);
    for (const auto& entry : prepared.manifest.models) {
        auto path = validateEntryPath(prepared.manifest.root / "models", entry.filename, error);
        if (!path) {
            return std::nullopt;
        }
        const std::string actualHash = hash::computeFile(*path);
        if (actualHash.empty() || actualHash != entry.hash) {
            error = fail(ProjectDirectoryImportError::HashMismatch,
                         "Model hash does not match manifest: " + entry.filename);
            return std::nullopt;
        }
        prepared.models.push_back({*path, actualHash});
    }

    for (const auto& filename : prepared.manifest.heightmaps) {
        if (!validateEntryPath(prepared.manifest.root / "heightmaps", filename, error)) {
            return std::nullopt;
        }
    }

    for (const auto& entry : prepared.manifest.gcode) {
        auto path = validateEntryPath(prepared.manifest.root / "gcode", entry.filename, error);
        if (!path) {
            return std::nullopt;
        }
        const std::string fileHash = hash::computeFile(*path);
        if (fileHash.empty()) {
            error = fail(ProjectDirectoryImportError::GCodeImportFailed,
                         "Cannot hash G-code entry: " + entry.filename);
            return std::nullopt;
        }

        GCodeLoader loader;
        auto loaded = loader.load(*path);
        if (!loaded) {
            error = fail(ProjectDirectoryImportError::InvalidGCode,
                         "Invalid G-code entry " + entry.filename + ": " + loaded.error);
            return std::nullopt;
        }

        GCodeRecord record;
        record.hash = fileHash;
        record.name = file::getStem(*path);
        record.fileSize = file::fileSize(*path);
        const auto& metadata = loader.lastMetadata();
        record.boundsMin = metadata.boundsMin;
        record.boundsMax = metadata.boundsMax;
        record.totalDistance = metadata.totalDistance;
        record.estimatedTime = metadata.estimatedTime;
        record.feedRates = metadata.feedRates;
        record.toolNumbers = metadata.toolNumbers;
        prepared.gcode.push_back({*path, fileHash, std::move(record)});
    }
    return prepared;
}

bool verifyHydration(ProjectRepository& projectRepository,
                     GCodeRepository& gcodeRepository,
                     i64 projectId,
                     const PreparedManifest& prepared,
                     const std::vector<i64>& modelIds,
                     const std::vector<i64>& gcodeIds) {
    auto record = projectRepository.findById(projectId);
    const Path storedProjectRoot =
        PathResolver::durableLocation(prepared.manifest.root, PathCategory::Projects);
    if (!record || record->name != prepared.manifest.name ||
        record->description != prepared.manifest.description ||
        record->filePath != storedProjectRoot ||
        projectRepository.getModelIds(projectId) != modelIds) {
        return false;
    }

    const auto linkedGCode = gcodeRepository.findByProject(projectId);
    if (linkedGCode.size() != gcodeIds.size()) {
        return false;
    }
    for (std::size_t index = 0; index < gcodeIds.size(); ++index) {
        if (linkedGCode[index].id != gcodeIds[index]) {
            return false;
        }
    }

    return true;
}

} // namespace

ProjectDirectoryImporter::ProjectDirectoryImporter(Database& database, LibraryManager& library)
    : m_database(database), m_library(library) {}

ProjectDirectoryImportResult ProjectDirectoryImporter::hydrate(const ProjectDirectory& directory) {
    ProjectDirectoryImportResult error;
    auto prepared = preflight(directory, error);
    if (!prepared) {
        return error;
    }

    if (!m_database.beginTransaction()) {
        return fail(ProjectDirectoryImportError::TransactionBeginFailed,
                    "Failed to begin project import transaction");
    }

    ProjectRepository projectRepository(m_database);
    GCodeRepository gcodeRepository(m_database);
    std::optional<i64> projectId;
    auto rollback = [&](ProjectDirectoryImportError cause, const std::string& message) {
        bool cleaned = m_database.rollback();
        if (projectId && projectRepository.findById(*projectId)) {
            cleaned = projectRepository.remove(*projectId) && cleaned;
        }
        if (!cleaned) {
            return fail(ProjectDirectoryImportError::ProjectRollbackFailed,
                        message + "; failed to fully roll back project import");
        }
        return fail(cause, message);
    };

    std::vector<i64> modelIds;
    std::unordered_map<std::string, i64> modelsByHash;
    for (const auto& model : prepared->models) {
        auto known = modelsByHash.find(model.hash);
        if (known != modelsByHash.end()) {
            continue;
        }
        auto existing = m_library.getModelByHash(model.hash);
        i64 modelId = 0;
        if (existing) {
            modelId = existing->id;
        } else {
            auto imported = m_library.importModel(model.path);
            if (!imported.success || imported.modelId <= 0) {
                return rollback(ProjectDirectoryImportError::ModelImportFailed,
                                imported.error.empty() ? "Failed to import project model"
                                                       : imported.error);
            }
            modelId = imported.modelId;
        }
        modelsByHash.emplace(model.hash, modelId);
        modelIds.push_back(modelId);
    }

    std::vector<i64> gcodeIds;
    std::unordered_map<std::string, i64> gcodeByHash;
    for (const auto& gcode : prepared->gcode) {
        auto known = gcodeByHash.find(gcode.hash);
        if (known != gcodeByHash.end()) {
            continue;
        }
        auto existing = gcodeRepository.findByHash(gcode.hash);
        i64 gcodeId = 0;
        if (existing) {
            gcodeId = existing->id;
        } else {
            const Path storageRoot = Config::instance().getGCodeDir();
            if (!file::createDirectories(storageRoot)) {
                return rollback(ProjectDirectoryImportError::GCodeStorageFailed,
                                "Cannot create configured G-code directory");
            }
            std::string extension = gcode.path.extension().string();
            if (extension.empty())
                extension = ".nc";
            const Path destination = storageRoot / (gcode.hash + extension);
            if (file::exists(destination)) {
                if (!file::isFile(destination) || hash::computeFile(destination) != gcode.hash) {
                    return rollback(ProjectDirectoryImportError::GCodeStorageFailed,
                                    "Stored G-code path has conflicting content");
                }
            } else if (!file::copy(gcode.path, destination) ||
                       hash::computeFile(destination) != gcode.hash) {
                return rollback(ProjectDirectoryImportError::GCodeStorageFailed,
                                "Failed to copy G-code into configured storage");
            }

            GCodeRecord record = gcode.record;
            record.filePath = PathResolver::makeStorable(destination, PathCategory::GCode);
            auto inserted = gcodeRepository.insert(record);
            if (!inserted) {
                existing = gcodeRepository.findByHash(gcode.hash);
                if (!existing) {
                    return rollback(ProjectDirectoryImportError::GCodeImportFailed,
                                    "Failed to register imported G-code");
                }
                gcodeId = existing->id;
            } else {
                gcodeId = *inserted;
            }
        }
        gcodeByHash.emplace(gcode.hash, gcodeId);
        gcodeIds.push_back(gcodeId);
    }

    ProjectRecord project;
    project.name = prepared->manifest.name;
    project.description = prepared->manifest.description;
    project.filePath =
        PathResolver::durableLocation(prepared->manifest.root, PathCategory::Projects);
    projectId = projectRepository.insert(project);
    if (!projectId) {
        return rollback(ProjectDirectoryImportError::ProjectCreateFailed,
                        "Failed to create project record");
    }

    for (std::size_t index = 0; index < modelIds.size(); ++index) {
        if (!projectRepository.addModel(*projectId, modelIds[index], static_cast<int>(index))) {
            return rollback(ProjectDirectoryImportError::ProjectAssociationFailed,
                            "Failed to associate model with imported project");
        }
    }

    for (std::size_t index = 0; index < gcodeIds.size(); ++index) {
        if (!gcodeRepository.addToProject(*projectId, gcodeIds[index], static_cast<int>(index))) {
            return rollback(ProjectDirectoryImportError::ProjectAssociationFailed,
                            "Failed to associate G-code with imported project");
        }
    }

    if (!verifyHydration(
            projectRepository, gcodeRepository, *projectId, *prepared, modelIds, gcodeIds)) {
        return rollback(ProjectDirectoryImportError::ProjectVerificationFailed,
                        "Imported project did not verify before commit");
    }
    if (!m_database.commit()) {
        return rollback(ProjectDirectoryImportError::TransactionCommitFailed,
                        "Failed to commit imported project");
    }

    ProjectDirectoryImportResult result;
    result.projectId = projectId;
    result.canonicalRoot = prepared->manifest.root;
    result.modelCount = modelIds.size();
    result.gcodeCount = gcodeIds.size();
    return result;
}

} // namespace dw
