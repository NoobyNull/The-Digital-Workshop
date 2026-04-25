#include "project.h"

#include <algorithm>
#include <filesystem>

#include "project_directory.h"
#include "../config/config.h"
#include "../database/database.h"
#include "../database/gcode_repository.h"
#include "../database/model_repository.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

namespace dw {

// Project implementation

void Project::addModel(i64 modelId) {
    if (!hasModel(modelId)) {
        m_modelIds.push_back(modelId);
        m_modified = true;
    }
}

void Project::removeModel(i64 modelId) {
    auto it = std::find(m_modelIds.begin(), m_modelIds.end(), modelId);
    if (it != m_modelIds.end()) {
        m_modelIds.erase(it);
        m_modified = true;
    }
}

void Project::reorderModel(i64 modelId, int newPosition) {
    auto it = std::find(m_modelIds.begin(), m_modelIds.end(), modelId);
    if (it == m_modelIds.end()) {
        return;
    }

    m_modelIds.erase(it);

    if (newPosition < 0) {
        newPosition = 0;
    }
    if (newPosition > static_cast<int>(m_modelIds.size())) {
        newPosition = static_cast<int>(m_modelIds.size());
    }

    m_modelIds.insert(m_modelIds.begin() + newPosition, modelId);
    m_modified = true;
}

bool Project::hasModel(i64 modelId) const {
    return std::find(m_modelIds.begin(), m_modelIds.end(), modelId) != m_modelIds.end();
}

// ProjectManager implementation

ProjectManager::ProjectManager(Database& db) : m_db(db), m_projectRepo(db) {}

std::shared_ptr<Project> ProjectManager::create(const std::string& name) {
    ProjectRecord record;
    record.name = name;

    auto id = m_projectRepo.insert(record);
    if (!id) {
        log::error("Project", "Failed to create in database");
        return nullptr;
    }

    auto project = std::make_shared<Project>();
    project->record().id = *id;
    project->record().name = name;

    log::infof("Project", "Created: %s (ID: %lld)", name.c_str(), static_cast<long long>(*id));

    return project;
}

std::shared_ptr<Project> ProjectManager::open(i64 projectId) {
    auto record = m_projectRepo.findById(projectId);
    if (!record) {
        log::errorf("Project", "Not found: %lld", static_cast<long long>(projectId));
        return nullptr;
    }

    auto project = std::make_shared<Project>();
    project->record() = *record;

    // Load model IDs
    auto modelIds = m_projectRepo.getModelIds(projectId);
    for (i64 id : modelIds) {
        project->addModel(id);
    }
    m_projectRepo.ensureOpenItemsForProject(projectId);
    project->clearModified();

    log::infof("Project",
               "Opened: %s (ID: %lld)",
               record->name.c_str(),
               static_cast<long long>(projectId));

    return project;
}

bool ProjectManager::save(Project& project) {
    if (!ensureProjectDirectory(project)) {
        log::error("Project", "Failed to prepare project directory");
        return false;
    }

    // Update project record
    if (!m_projectRepo.update(project.record())) {
        log::error("Project", "Failed to update record");
        return false;
    }

    // Sync model associations
    auto currentDbIds = m_projectRepo.getModelIds(project.id());

    // Remove models no longer in project
    for (i64 dbId : currentDbIds) {
        if (!project.hasModel(dbId)) {
            m_projectRepo.removeModel(project.id(), dbId);
        }
    }

    // Add new models and update order
    const auto& projectIds = project.modelIds();
    for (int i = 0; i < static_cast<int>(projectIds.size()); ++i) {
        i64 modelId = projectIds[static_cast<usize>(i)];
        if (!m_projectRepo.hasModel(project.id(), modelId)) {
            m_projectRepo.addModel(project.id(), modelId, i);
        } else {
            m_projectRepo.updateModelOrder(project.id(), modelId, i);
        }
    }

    m_projectRepo.ensureOpenItemsForProject(project.id());
    syncProjectDirectory(project);

    project.clearModified();
    log::infof("Project", "Saved: %s", project.name().c_str());
    return true;
}

bool ProjectManager::close(Project& project) {
    if (project.isModified()) {
        // Caller should handle save confirmation
        log::warning("Project", "Closing modified project without saving");
    }

    log::infof("Project", "Closed: %s", project.name().c_str());
    return true;
}

bool ProjectManager::remove(i64 projectId) {
    if (!m_projectRepo.remove(projectId)) {
        log::errorf("Project", "Failed to remove: %lld", static_cast<long long>(projectId));
        return false;
    }

    log::infof("Project", "Removed: %lld", static_cast<long long>(projectId));
    return true;
}

std::vector<ProjectRecord> ProjectManager::listProjects() {
    return m_projectRepo.findAll();
}

std::optional<ProjectRecord> ProjectManager::getProjectInfo(i64 projectId) {
    return m_projectRepo.findById(projectId);
}

std::vector<ProjectOpenItem> ProjectManager::listOpenItems(i64 projectId) {
    m_projectRepo.ensureOpenItemsForProject(projectId);
    m_projectRepo.validateOpenItemsForProject(projectId);
    return m_projectRepo.listOpenItemsForProject(projectId);
}

std::vector<ProjectOpenItem> ProjectManager::currentOpenItems() {
    if (!m_currentProject) {
        return {};
    }
    return listOpenItems(m_currentProject->id());
}

bool ProjectManager::addModelToProject(i64 modelId) {
    if (!m_currentProject) {
        log::warning("Project", "No project open");
        return false;
    }

    m_currentProject->addModel(modelId);
    return true;
}

bool ProjectManager::removeModelFromProject(i64 modelId) {
    if (!m_currentProject) {
        log::warning("Project", "No project open");
        return false;
    }

    m_currentProject->removeModel(modelId);
    return true;
}

Path ProjectManager::canonicalProjectDirectory(const Project& project) const {
    Path filePath = project.filePath();
    if (!filePath.empty() && filePath.extension().empty()) {
        return filePath;
    }

    std::string baseName = project.name().empty() ? "project" : project.name();
    return Config::instance().getProjectsDir() / ProjectDirectory::sanitizeName(baseName);
}

bool ProjectManager::ensureProjectDirectory(Project& project) {
    Path root = canonicalProjectDirectory(project);
    if (root.empty()) {
        return false;
    }

    auto dir = std::make_shared<ProjectDirectory>();
    if (file::isDirectory(root) && file::exists(root / "project.json")) {
        if (!dir->open(root)) {
            return false;
        }
        dir->setMetadata(project.name(), project.description());
    } else {
        if (!dir->create(root, project.name(), project.description())) {
            return false;
        }
    }

    project.setFilePath(root);
    m_currentDir = dir;
    return true;
}

Path ProjectManager::resolveModelPath(const ModelRecord& model) const {
    Path resolved = PathResolver::resolve(model.filePath, PathCategory::Support);
    if (file::isFile(resolved)) {
        return resolved;
    }
    resolved = PathResolver::resolve(model.filePath, PathCategory::Models);
    if (file::isFile(resolved)) {
        return resolved;
    }
    return model.filePath;
}

Path ProjectManager::resolveGCodePath(const GCodeRecord& gcode) const {
    return PathResolver::resolve(gcode.filePath, PathCategory::GCode);
}

bool ProjectManager::syncProjectDirectory(Project& project) {
    if (!m_currentDir || m_currentDir->root() != project.filePath()) {
        if (!ensureProjectDirectory(project)) {
            return false;
        }
    }

    m_currentDir->setMetadata(project.name(), project.description());
    m_currentDir->clearModels();
    m_currentDir->clearGCode();

    ModelRepository modelRepo(m_db);
    for (i64 modelId : project.modelIds()) {
        auto model = modelRepo.findById(modelId);
        if (!model) {
            log::warningf("Project",
                          "Project '%s' references missing model %lld",
                          project.name().c_str(),
                          static_cast<long long>(modelId));
            continue;
        }

        Path sourcePath = resolveModelPath(*model);
        if (!m_currentDir->addModelFile(sourcePath, model->hash)) {
            log::warningf("Project",
                          "Could not mirror model '%s' into project directory",
                          model->name.c_str());
        }
    }

    GCodeRepository gcodeRepo(m_db);
    for (const auto& gcode : gcodeRepo.findByProject(project.id())) {
        Path sourcePath = resolveGCodePath(gcode);
        if (!m_currentDir->addGCodeFile(sourcePath)) {
            log::warningf("Project",
                          "Could not mirror G-code '%s' into project directory",
                          gcode.name.c_str());
        }
    }

    return m_currentDir->save();
}

std::shared_ptr<ProjectDirectory> ProjectManager::ensureProjectForModel(
    const std::string& modelName, const Path& modelSourcePath) {

    std::string dirName = ProjectDirectory::sanitizeName(modelName);

    // Check if a permanent project already exists for this model
    Path permanentRoot = Config::instance().getProjectsDir() / dirName;
    if (file::isDirectory(permanentRoot) && file::exists(permanentRoot / "project.json")) {
        auto dir = std::make_shared<ProjectDirectory>();
        if (!dir->open(permanentRoot)) {
            log::errorf("Project", "Failed to open existing project dir: %s",
                        permanentRoot.c_str());
            return nullptr;
        }

        // Sync with SQLite
        auto records = m_projectRepo.findAll();
        std::shared_ptr<Project> project;
        for (const auto& rec : records) {
            if (rec.filePath == permanentRoot) {
                project = open(rec.id);
                break;
            }
        }
        if (!project) {
            project = create(modelName);
            if (project) {
                project->setFilePath(permanentRoot);
                save(*project);
            }
        }
        if (project) {
            m_currentProject = project;
        }
        m_currentDir = dir;
        return dir;
    }

    // If the current temp dir already matches this model, reuse it
    if (m_currentDir && m_currentDir->name() == modelName && m_currentProject &&
        m_currentProject->isTemporary()) {
        return m_currentDir;
    }

    // Create directly in the projects directory (same location as permanent projects)
    Path tempRoot = Config::instance().getProjectsDir() / dirName;

    auto dir = std::make_shared<ProjectDirectory>();
    if (file::isDirectory(tempRoot) && file::exists(tempRoot / "project.json")) {
        if (!dir->open(tempRoot)) {
            log::errorf("Project", "Failed to open temp project dir: %s",
                        tempRoot.c_str());
            return nullptr;
        }
    } else {
        if (!dir->create(tempRoot, modelName)) {
            log::errorf("Project", "Failed to create temp project dir: %s",
                        tempRoot.c_str());
            return nullptr;
        }
        if (!modelSourcePath.empty() && file::isFile(modelSourcePath)) {
            dir->addModelFile(modelSourcePath);
            dir->save();
        }
    }

    // Create a lightweight DB record but mark as temporary
    auto project = create(modelName);
    if (project) {
        project->setFilePath(tempRoot);
        project->setTemporary(true);
        save(*project);
        m_currentProject = project;
    }
    m_currentDir = dir;

    // Don't add temp projects to recent projects list
    return dir;
}

bool ProjectManager::saveTemporaryProject() {
    if (!m_currentProject || !m_currentProject->isTemporary() || !m_currentDir)
        return false;

    std::string dirName = ProjectDirectory::sanitizeName(m_currentProject->name());
    Path permanentRoot = Config::instance().getProjectsDir() / dirName;
    Path tempRoot = m_currentDir->root();

    // Move temp directory to permanent location
    std::error_code ec;
    std::filesystem::create_directories(permanentRoot.parent_path(), ec);
    if (ec) {
        log::errorf("Project", "Failed to create parent dir: %s", ec.message().c_str());
        return false;
    }

    // If permanent already exists (race), remove it first
    if (std::filesystem::exists(permanentRoot)) {
        std::filesystem::remove_all(permanentRoot, ec);
    }

    std::filesystem::rename(tempRoot, permanentRoot, ec);
    if (ec) {
        // rename fails across filesystems — fall back to copy+delete
        std::filesystem::copy(tempRoot, permanentRoot,
                              std::filesystem::copy_options::recursive, ec);
        if (ec) {
            log::errorf("Project", "Failed to copy project to permanent: %s",
                        ec.message().c_str());
            return false;
        }
        std::filesystem::remove_all(tempRoot, ec);
    }

    // Update project record to point to permanent location
    m_currentProject->setFilePath(permanentRoot);
    m_currentProject->setTemporary(false);
    save(*m_currentProject);

    // Re-open directory at new location
    m_currentDir = std::make_shared<ProjectDirectory>();
    m_currentDir->open(permanentRoot);

    // Now add to recent projects
    Config::instance().addRecentProject(permanentRoot);
    Config::instance().save();

    log::infof("Project", "Saved temporary project to: %s", permanentRoot.c_str());
    return true;
}

void ProjectManager::discardTemporaryProject() {
    if (!m_currentProject || !m_currentProject->isTemporary())
        return;

    Path tempRoot;
    if (m_currentDir)
        tempRoot = m_currentDir->root();

    // Remove from DB
    remove(m_currentProject->id());

    m_currentProject.reset();
    m_currentDir.reset();

    // Clean up temp files
    if (!tempRoot.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
    }
}

} // namespace dw
