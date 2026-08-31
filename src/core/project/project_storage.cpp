#include "project.h"

#include <filesystem>
#include <string>

#include "../config/config.h"
#include "../database/gcode_repository.h"
#include "../database/model_repository.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "project_directory.h"

namespace dw {
namespace {

constexpr const char* kTemporaryMarker = ".dw-temporary-project";

Path availablePermanentRoot(const Project& project) {
    const std::string baseName = project.name().empty() ? "project" : project.name();
    const std::string slug = ProjectDirectory::sanitizeName(baseName);
    const Path projectsRoot = Config::instance().getProjectsDir();
    Path candidate = projectsRoot / slug;
    if (!file::exists(candidate))
        return candidate;

    candidate = projectsRoot / (slug + "-" + std::to_string(project.id()));
    int suffix = 2;
    while (file::exists(candidate)) {
        candidate = projectsRoot /
                    (slug + "-" + std::to_string(project.id()) + "-" + std::to_string(suffix++));
    }
    return candidate;
}

Path temporaryRoot(const Project& project) {
    const std::string baseName = project.name().empty() ? "project" : project.name();
    const std::string slug = ProjectDirectory::sanitizeName(baseName);
    return Config::instance().getProjectsDir() / ".temporary" /
           (slug + "-" + std::to_string(project.id()));
}

Path markerPath(const Path& root) {
    return root / kTemporaryMarker;
}

bool hasTemporaryMarker(const Path& root, i64 projectId) {
    const auto marker = file::readText(markerPath(root));
    return marker.has_value() && *marker == std::to_string(projectId);
}

bool writeTemporaryMarker(const Path& root, i64 projectId) {
    return file::writeTextAtomic(markerPath(root), std::to_string(projectId));
}

Path normalizedPath(const Path& path) {
    std::error_code error;
    const Path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

bool samePath(const Path& lhs, const Path& rhs) {
    return normalizedPath(lhs) == normalizedPath(rhs);
}

bool isOwnedTemporaryRoot(const Path& root) {
    const Path temporaryParent = Config::instance().getProjectsDir() / ".temporary";
    return normalizedPath(root).parent_path() == normalizedPath(temporaryParent);
}

bool hasManagedProjectDirectories(const ProjectDirectory& directory) {
    return file::isDirectory(directory.modelsDir()) &&
           file::isDirectory(directory.heightmapsDir()) &&
           file::isDirectory(directory.gcodeDir()) &&
           file::isDirectory(directory.imagesDir()) &&
           file::isDirectory(directory.costingDir());
}

std::optional<Path> copyToOwnedStaging(const Path& source, i64 projectId) {
    const Path parent = Config::instance().getProjectsDir();
    if (!file::createDirectories(parent))
        return std::nullopt;

    Path staging;
    for (int attempt = 0; attempt < 100; ++attempt) {
        staging = parent /
                  (".promoting-" + std::to_string(projectId) + "-" + std::to_string(attempt));
        std::error_code createError;
        if (std::filesystem::create_directory(staging, createError))
            break;
        if (attempt == 99)
            return std::nullopt;
    }

    std::error_code copyError;
    for (const auto& entry : std::filesystem::directory_iterator(source, copyError)) {
        if (copyError)
            break;
        std::filesystem::copy(entry.path(),
                              staging / entry.path().filename(),
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::copy_symlinks,
                              copyError);
        if (copyError)
            break;
    }
    if (!copyError)
        return staging;

    log::errorf("Project", "Failed to stage project promotion: %s", copyError.message().c_str());
    std::error_code cleanupError;
    std::filesystem::remove_all(staging, cleanupError);
    return std::nullopt;
}

} // namespace

Path ProjectManager::canonicalProjectDirectory(const Project& project) const {
    if (!project.filePath().empty())
        return project.filePath();
    if (project.isTemporary())
        return temporaryRoot(project);
    return availablePermanentRoot(project);
}

ProjectStorageValidationStatus ProjectManager::validateProjectStorage(i64 projectId) {
    const auto record = projectId > 0 ? m_projectRepo.findById(projectId) : std::nullopt;
    if (!record)
        return ProjectStorageValidationStatus::MissingRecord;

    const Path root =
        PathResolver::resolve(record->filePath, PathCategory::Projects);
    if (root.empty() || !file::exists(root))
        return ProjectStorageValidationStatus::MissingPath;
    if (!file::isDirectory(root))
        return ProjectStorageValidationStatus::InvalidDirectory;

    ProjectDirectory directory;
    if (!directory.inspect(root) || !hasManagedProjectDirectories(directory))
        return ProjectStorageValidationStatus::InvalidDirectory;
    if (directory.projectId() != projectId)
        return ProjectStorageValidationStatus::IdentityMismatch;
    if (directoryClaimedByOtherRecord(root, projectId))
        return ProjectStorageValidationStatus::DuplicateClaim;
    if (record->temporary &&
        (!isOwnedTemporaryRoot(root) || !hasTemporaryMarker(root, projectId))) {
        return ProjectStorageValidationStatus::InvalidTemporaryOwnership;
    }

    return ProjectStorageValidationStatus::Ready;
}

bool ProjectManager::ensureProjectDirectory(Project& project) {
    const Path root = canonicalProjectDirectory(project);
    if (root.empty())
        return false;

    const bool existed = file::exists(root);
    auto directory = std::make_shared<ProjectDirectory>();
    if (file::isDirectory(root) && file::exists(root / "project.json")) {
        if (project.isTemporary() &&
            (!isOwnedTemporaryRoot(root) || !hasTemporaryMarker(root, project.id()))) {
            log::errorf("Project",
                        "Refusing to reuse unowned directory for temporary project: %s",
                        root.c_str());
            return false;
        }
        if (!directory->open(root))
            return false;
        if (directoryClaimedByOtherRecord(root, project.id())) {
            log::errorf("Project", "Project directory already has a local owner: %s", root.c_str());
            return false;
        }
        directory->setMetadata(project.name(), project.description());
    } else {
        if (existed) {
            log::errorf("Project", "Refusing to overwrite non-project directory: %s", root.c_str());
            return false;
        }
        if (!directory->create(root, project.name(), project.description()))
            return false;
    }

    directory->setProjectId(project.id());

    if (project.isTemporary() && !writeTemporaryMarker(root, project.id())) {
        log::errorf("Project", "Failed to mark temporary project directory: %s", root.c_str());
        return false;
    }

    project.setFilePath(root);
    if (m_currentProject && m_currentProject->id() == project.id())
        m_currentDir = std::move(directory);
    return true;
}

bool ProjectManager::directoryClaimedByOtherRecord(const Path& root, i64 projectId) {
    const Path durableRoot =
        PathResolver::durableLocation(root, PathCategory::Projects);
    for (const auto& record : m_projectRepo.findAll()) {
        if (record.id == projectId || record.filePath.empty())
            continue;
        const Path durableRecordPath =
            PathResolver::durableLocation(record.filePath, PathCategory::Projects);
        if (samePath(durableRecordPath, durableRoot)) {
            return true;
        }
    }
    return false;
}

Path ProjectManager::resolveModelPath(const ModelRecord& model) const {
    Path resolved = PathResolver::resolve(model.filePath, PathCategory::Support);
    if (file::isFile(resolved))
        return resolved;
    resolved = PathResolver::resolve(model.filePath, PathCategory::Models);
    if (file::isFile(resolved))
        return resolved;
    return model.filePath;
}

Path ProjectManager::resolveGCodePath(const GCodeRecord& gcode) const {
    return PathResolver::resolve(gcode.filePath, PathCategory::GCode);
}

bool ProjectManager::syncProjectDirectory(Project& project) {
    if (project.filePath().empty() && !ensureProjectDirectory(project))
        return false;

    ProjectDirectory candidate;
    const Path root = project.filePath();
    if (file::isDirectory(root) && file::exists(root / "project.json")) {
        if (!candidate.open(root))
            return false;
    } else if (!candidate.create(root, project.name(), project.description())) {
        return false;
    }

    candidate.setMetadata(project.name(), project.description());
    candidate.setProjectId(project.id());
    candidate.clearModels();
    candidate.clearGCode();

    ModelRepository modelRepository(m_db);
    for (i64 modelId : project.modelIds()) {
        const auto model = modelRepository.findById(modelId);
        if (!model) {
            log::errorf("Project",
                        "Project '%s' references missing model %lld",
                        project.name().c_str(),
                        static_cast<long long>(modelId));
            return false;
        }

        const Path sourcePath = resolveModelPath(*model);
        const bool added = model->hash.empty() ? candidate.addModelFile(sourcePath)
                                               : candidate.addModelFile(sourcePath, model->hash);
        if (!added) {
            log::errorf("Project",
                        "Could not mirror model '%s' into project directory",
                        model->name.c_str());
            return false;
        }
    }

    GCodeRepository gcodeRepository(m_db);
    for (const auto& gcode : gcodeRepository.findByProject(project.id())) {
        if (!candidate.addGCodeFile(resolveGCodePath(gcode))) {
            log::errorf("Project",
                        "Could not mirror G-code '%s' into project directory",
                        gcode.name.c_str());
            return false;
        }
    }

    if (!candidate.save())
        return false;
    if (m_currentProject && m_currentProject->id() == project.id()) {
        m_currentDir = std::make_shared<ProjectDirectory>(std::move(candidate));
    }
    return true;
}

bool ProjectManager::saveTemporaryProject() {
    if (!m_currentProject || !m_currentProject->isTemporary() || !m_currentDir)
        return false;

    Project& project = *m_currentProject;
    if (!save(project))
        return false;

    const Path sourceRoot = project.filePath();
    if (!hasTemporaryMarker(sourceRoot, project.id())) {
        log::error("Project", "Temporary project ownership marker is missing");
        return false;
    }

    auto stagingRoot = copyToOwnedStaging(sourceRoot, project.id());
    if (!stagingRoot)
        return false;
    if (!hasTemporaryMarker(*stagingRoot, project.id())) {
        std::error_code cleanupError;
        std::filesystem::remove_all(*stagingRoot, cleanupError);
        log::error("Project", "Staged project failed ownership verification");
        return false;
    }
    std::error_code markerError;
    std::filesystem::remove(markerPath(*stagingRoot), markerError);
    if (file::exists(markerPath(*stagingRoot))) {
        log::errorf("Project",
                    "Could not remove temporary ownership marker: %s",
                    markerError.message().c_str());
        std::error_code cleanupError;
        std::filesystem::remove_all(*stagingRoot, cleanupError);
        return false;
    }

    Path destinationRoot;
    for (int attempt = 0; attempt < 100; ++attempt) {
        destinationRoot = availablePermanentRoot(project);
        std::error_code publishError;
        std::filesystem::rename(*stagingRoot, destinationRoot, publishError);
        if (!publishError)
            break;
        if (!file::exists(destinationRoot) || attempt == 99) {
            log::errorf("Project",
                        "Failed to publish promoted project: %s",
                        publishError.message().c_str());
            std::error_code cleanupError;
            std::filesystem::remove_all(*stagingRoot, cleanupError);
            return false;
        }
    }

    project.setFilePath(destinationRoot);
    project.setTemporary(false);

    if (!save(project)) {
        project.setTemporary(true);
        project.setFilePath(sourceRoot);
        std::error_code cleanupError;
        std::filesystem::remove_all(destinationRoot, cleanupError);
        auto restoredDirectory = std::make_shared<ProjectDirectory>();
        if (restoredDirectory->open(project.filePath()))
            m_currentDir = std::move(restoredDirectory);
        return false;
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(sourceRoot, cleanupError);
    if (cleanupError) {
        log::warningf("Project",
                      "Promoted project but could not remove temporary copy: %s",
                      cleanupError.message().c_str());
    }

    Config::instance().addRecentProject(project.filePath());
    Config::instance().save();
    log::infof("Project", "Promoted temporary project: %s", project.filePath().c_str());
    return true;
}

bool ProjectManager::discardTemporaryProjectData() {
    if (!m_currentProject || !m_currentProject->isTemporary())
        return false;

    const Path root = m_currentDir ? m_currentDir->root() : m_currentProject->filePath();
    return discardTemporaryProjectData(m_currentProject->id(), root);
}

bool ProjectManager::discardTemporaryProjectData(i64 projectId, const Path& projectRoot) {
    const auto record = projectId > 0 ? m_projectRepo.findById(projectId) : std::nullopt;
    if (!record || !record->temporary || projectRoot.empty() ||
        !samePath(record->filePath, projectRoot) || !isOwnedTemporaryRoot(projectRoot) ||
        !hasTemporaryMarker(projectRoot, projectId)) {
        log::error("Project", "Refusing to discard unowned temporary project data");
        return false;
    }

    if (!remove(projectId) || m_projectRepo.findById(projectId).has_value())
        return false;

    std::error_code cleanupError;
    std::filesystem::remove_all(projectRoot, cleanupError);
    if (cleanupError) {
        log::warningf("Project",
                      "Temporary project record removed; deferred file cleanup: %s",
                      cleanupError.message().c_str());
    }
    return true;
}

} // namespace dw
