#include "project.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>

#include "../database/database.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "project_directory.h"

namespace dw {
namespace {

constexpr std::size_t kMaximumProjectNameLength = 96;

std::string normalizedProjectName(std::string_view value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
                          return std::isspace(c) != 0;
                      }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

bool validProjectName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." ||
        name.size() > kMaximumProjectNameLength) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char raw) {
        const auto c = static_cast<unsigned char>(raw);
        return c < 0x20 || c == 0x7f || c == '/' || c == '\\';
    });
}

} // namespace

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

void ProjectManager::synchronizeActiveProject(std::shared_ptr<Project> project) {
    std::shared_ptr<ProjectDirectory> directory;

    if (project && !project->filePath().empty()) {
        const Path root =
            PathResolver::resolve(project->filePath(), PathCategory::Projects);
        project->setFilePath(root);
        if (file::isDirectory(root) && file::exists(root / "project.json")) {
            auto candidate = std::make_shared<ProjectDirectory>();
            if (candidate->open(root) && !directoryClaimedByOtherRecord(root, project->id())) {
                directory = std::move(candidate);
            }
        }
    }

    m_currentProject = std::move(project);
    m_currentDir = std::move(directory);
}

std::shared_ptr<Project> ProjectManager::create(const std::string& name, bool temporary) {
    const std::string normalizedName = normalizedProjectName(name);
    if (!validProjectName(normalizedName)) {
        log::warning("Project", "Refusing to create a project with an invalid name");
        return nullptr;
    }

    ProjectRecord record;
    record.name = normalizedName;
    record.temporary = temporary;

    auto id = m_projectRepo.insert(record);
    if (!id) {
        log::error("Project", "Failed to create in database");
        return nullptr;
    }

    auto project = std::make_shared<Project>();
    project->record().id = *id;
    project->record().name = normalizedName;
    project->record().temporary = temporary;

    log::infof("Project",
               "Created: %s (ID: %lld)",
               normalizedName.c_str(),
               static_cast<long long>(*id));

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
    project->setFilePath(
        PathResolver::resolve(record->filePath, PathCategory::Projects));

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
    const Path originalPath = project.filePath();
    const Path intendedRoot = canonicalProjectDirectory(project);
    const bool rootExisted = file::exists(intendedRoot);
    const auto originalManifest = rootExisted ? file::readText(intendedRoot / "project.json")
                                              : Result<std::string>{std::nullopt};
    const auto originalDirectory = m_currentDir;
    auto failSave = [&]() {
        const Path preparedRoot = project.filePath();
        if (!rootExisted && !preparedRoot.empty()) {
            std::error_code cleanupError;
            std::filesystem::remove_all(preparedRoot, cleanupError);
        } else if (rootExisted && originalManifest.has_value()) {
            (void)file::writeTextAtomic(intendedRoot / "project.json", *originalManifest);
        }
        project.setFilePath(originalPath);
        if (m_currentProject && m_currentProject->id() == project.id())
            m_currentDir = originalDirectory;
        return false;
    };

    if (!ensureProjectDirectory(project)) {
        log::error("Project", "Failed to prepare project directory");
        return failSave();
    }

    Transaction transaction(m_db);

    // Persist a stable location while retaining the materialized filesystem
    // path on the live Project used by the save/sync operations below.
    ProjectRecord persistedRecord = project.record();
    persistedRecord.filePath =
        PathResolver::durableLocation(project.filePath(), PathCategory::Projects);
    if (!m_projectRepo.update(persistedRecord)) {
        log::error("Project", "Failed to update record");
        return failSave();
    }

    // Sync model associations
    auto currentDbIds = m_projectRepo.getModelIds(project.id());

    // Remove models no longer in project
    for (i64 dbId : currentDbIds) {
        if (!project.hasModel(dbId)) {
            if (!m_projectRepo.removeModel(project.id(), dbId)) {
                log::errorf("Project",
                            "Failed to remove model association %lld",
                            static_cast<long long>(dbId));
                return failSave();
            }
        }
    }

    // Add new models and update order
    const auto& projectIds = project.modelIds();
    for (int i = 0; i < static_cast<int>(projectIds.size()); ++i) {
        i64 modelId = projectIds[static_cast<usize>(i)];
        if (!m_projectRepo.hasModel(project.id(), modelId)) {
            if (!m_projectRepo.addModel(project.id(), modelId, i)) {
                log::errorf("Project",
                            "Failed to add model association %lld",
                            static_cast<long long>(modelId));
                return failSave();
            }
        } else {
            if (!m_projectRepo.updateModelOrder(project.id(), modelId, i)) {
                log::errorf("Project",
                            "Failed to reorder model association %lld",
                            static_cast<long long>(modelId));
                return failSave();
            }
        }
    }

    m_projectRepo.ensureOpenItemsForProject(project.id());
    if (!syncProjectDirectory(project)) {
        log::error("Project", "Failed to synchronize project directory");
        return failSave();
    }
    if (!transaction.commit()) {
        log::error("Project", "Failed to commit project save");
        return failSave();
    }

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

std::optional<ProjectOpenItem> ProjectManager::findOpenItem(i64 itemId) {
    return m_projectRepo.findOpenItemById(itemId);
}

std::optional<ProjectOpenItem> ProjectManager::findOpenItemBySource(
    std::string_view sourceTable, i64 sourceId) {
    if (!m_currentProject)
        return std::nullopt;
    auto items =
        m_projectRepo.findOpenItemsBySource(m_currentProject->id(), sourceTable, sourceId);
    return items.empty() ? std::nullopt : std::optional<ProjectOpenItem>{items.front()};
}

bool ProjectManager::updateOpenItem(ProjectOpenItem item) {
    if (!m_currentProject) {
        log::warning("Project", "Cannot update an open item without an active project");
        return false;
    }
    if (item.id <= 0 || item.projectId <= 0) {
        log::warning("Project", "Cannot update an open item without an exact row identity");
        return false;
    }
    if (item.projectId != m_currentProject->id()) {
        log::warning("Project", "Cannot update an open item from a non-active project");
        return false;
    }

    const auto persisted = m_projectRepo.findOpenItemById(item.id);
    if (!persisted || persisted->projectId != item.projectId) {
        log::warning("Project", "Cannot update an open item with a foreign row identity");
        return false;
    }

    return m_projectRepo.updateOpenItem(item);
}

bool ProjectManager::removeOpenItem(i64 itemId) {
    if (!m_currentProject) {
        log::warning("Project", "Cannot remove an open item without an active project");
        return false;
    }
    if (itemId <= 0) {
        log::warning("Project", "Cannot remove an open item without an exact row identity");
        return false;
    }

    const auto persisted = m_projectRepo.findOpenItemById(itemId);
    if (!persisted || persisted->projectId != m_currentProject->id()) {
        log::warning("Project", "Cannot remove a missing or foreign open item");
        return false;
    }

    return m_projectRepo.removeOpenItem(itemId);
}

std::optional<i64> ProjectManager::upsertOpenItem(ProjectOpenItem item) {
    if (item.projectId <= 0) {
        log::warning("Project", "Cannot upsert open item without a project id");
        return std::nullopt;
    }
    if (!item.sourceTable.empty() && item.sourceId.has_value()) {
        return m_projectRepo.upsertOpenItemBySource(item);
    }
    return m_projectRepo.upsertOpenItemBySourceKey(item);
}

std::optional<i64> ProjectManager::upsertCurrentOpenItem(ProjectOpenItem item) {
    if (!m_currentProject) {
        log::warning("Project", "Cannot upsert open item without an open project");
        return std::nullopt;
    }

    item.projectId = m_currentProject->id();
    return upsertOpenItem(std::move(item));
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

} // namespace dw
