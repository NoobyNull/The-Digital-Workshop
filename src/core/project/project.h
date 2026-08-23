#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../database/model_repository.h"
#include "../database/project_repository.h"
#include "../types.h"

namespace dw {

// Forward declarations
class Database;
class ProjectDirectory;
class ProjectAssetMembershipService;
struct GCodeRecord;
struct ModelRecord;
struct ProjectAssetMembershipRequest;
struct ProjectAssetMembershipResult;

enum class ProjectStorageValidationStatus {
    Ready,
    MissingRecord,
    MissingPath,
    InvalidDirectory,
    IdentityMismatch,
    DuplicateClaim,
    InvalidTemporaryOwnership,
};

// Project class - represents an open project
class Project {
  public:
    Project() = default;

    // Project metadata
    i64 id() const { return m_record.id; }
    const std::string& name() const { return m_record.name; }
    void setName(const std::string& name) { m_record.name = name; }

    const std::string& description() const { return m_record.description; }
    void setDescription(const std::string& desc) { m_record.description = desc; }

    const Path& filePath() const { return m_record.filePath; }
    void setFilePath(const Path& path) { m_record.filePath = path; }

    const std::string& createdAt() const { return m_record.createdAt; }
    const std::string& modifiedAt() const { return m_record.modifiedAt; }

    // Model management
    const std::vector<i64>& modelIds() const { return m_modelIds; }
    void addModel(i64 modelId);
    void removeModel(i64 modelId);
    void reorderModel(i64 modelId, int newPosition);
    bool hasModel(i64 modelId) const;
    int modelCount() const { return static_cast<int>(m_modelIds.size()); }

    // Mark as modified
    void markModified() { m_modified = true; }
    bool isModified() const { return m_modified; }
    void clearModified() { m_modified = false; }

    // Temporary project (lives in system temp, not saved to permanent location)
    bool isTemporary() const { return m_record.temporary; }
    void setTemporary(bool temporary) { m_record.temporary = temporary; }

    // Internal record access
    const ProjectRecord& record() const { return m_record; }
    ProjectRecord& record() { return m_record; }

  private:
    ProjectRecord m_record;
    std::vector<i64> m_modelIds;
    bool m_modified = false;
};

// Project manager - handles project lifecycle
class ProjectManager {
  public:
    explicit ProjectManager(Database& db);

    // Project operations
    std::shared_ptr<Project> create(const std::string& name, bool temporary = false);
    std::shared_ptr<Project> open(i64 projectId);
    bool save(Project& project);
    bool close(Project& project);
    bool remove(i64 projectId);

    // Query projects
    std::vector<ProjectRecord> listProjects();
    std::optional<ProjectRecord> getProjectInfo(i64 projectId);
    [[nodiscard]] ProjectStorageValidationStatus validateProjectStorage(i64 projectId);
    std::vector<ProjectOpenItem> listOpenItems(i64 projectId);
    std::vector<ProjectOpenItem> currentOpenItems();
    std::optional<ProjectOpenItem> findOpenItem(i64 itemId);
    std::optional<ProjectOpenItem> findOpenItemBySource(std::string_view sourceTable,
                                                        i64 sourceId);
    // Persist changes to one existing open item in the active project. The row
    // identity is mandatory; this path never creates or source-key-upserts an item.
    bool updateOpenItem(ProjectOpenItem item);
    // Remove one exact open-item row owned by the active project. This rejects
    // missing, stale, and foreign row identities rather than deleting by source key.
    bool removeOpenItem(i64 itemId);
    std::optional<i64> upsertOpenItem(ProjectOpenItem item);
    std::optional<i64> upsertCurrentOpenItem(ProjectOpenItem item);

    // Current project
    std::shared_ptr<Project> currentProject() const { return m_currentProject; }
    // Compatibility seam for the ProjectSession integration adapter. Keeps the
    // legacy active-project pointer and its on-disk directory synchronized.
    void synchronizeActiveProject(std::shared_ptr<Project> project);

    // On-disk directory belonging to the synchronized active project.
    std::shared_ptr<ProjectDirectory> currentDirectory() const { return m_currentDir; }

    // Promote a temporary project to permanent storage
    bool saveTemporaryProject();

    // Clean up temporary project directory (call on discard)
    [[nodiscard]] bool discardTemporaryProjectData();
    [[nodiscard]] bool discardTemporaryProjectData(i64 projectId, const Path& projectRoot);

    // Model operations within current project
    bool addModelToProject(i64 modelId);
    bool removeModelFromProject(i64 modelId);

  private:
    friend class ProjectAssetMembershipService;

    [[nodiscard]] ProjectAssetMembershipResult
    persistAssetMembership(const ProjectAssetMembershipRequest& normalizedRequest);
    Path canonicalProjectDirectory(const Project& project) const;
    bool directoryClaimedByOtherRecord(const Path& root, i64 projectId);
    bool ensureProjectDirectory(Project& project);
    bool syncProjectDirectory(Project& project);
    Path resolveModelPath(const ModelRecord& model) const;
    Path resolveGCodePath(const GCodeRecord& gcode) const;

    Database& m_db;
    ProjectRepository m_projectRepo;
    std::shared_ptr<Project> m_currentProject;
    std::shared_ptr<ProjectDirectory> m_currentDir;
};

} // namespace dw
