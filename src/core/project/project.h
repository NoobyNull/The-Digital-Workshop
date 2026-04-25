#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../database/model_repository.h"
#include "../database/project_repository.h"
#include "../types.h"

namespace dw {

// Forward declarations
class Database;
class ProjectDirectory;
struct GCodeRecord;
struct ModelRecord;

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
    bool isTemporary() const { return m_temporary; }
    void setTemporary(bool temp) { m_temporary = temp; }

    // Internal record access
    const ProjectRecord& record() const { return m_record; }
    ProjectRecord& record() { return m_record; }

  private:
    ProjectRecord m_record;
    std::vector<i64> m_modelIds;
    bool m_modified = false;
    bool m_temporary = false;
};

// Project manager - handles project lifecycle
class ProjectManager {
  public:
    explicit ProjectManager(Database& db);

    // Project operations
    std::shared_ptr<Project> create(const std::string& name);
    std::shared_ptr<Project> open(i64 projectId);
    bool save(Project& project);
    bool close(Project& project);
    bool remove(i64 projectId);

    // Query projects
    std::vector<ProjectRecord> listProjects();
    std::optional<ProjectRecord> getProjectInfo(i64 projectId);
    std::vector<ProjectOpenItem> listOpenItems(i64 projectId);
    std::vector<ProjectOpenItem> currentOpenItems();
    std::optional<i64> upsertOpenItem(ProjectOpenItem item);
    std::optional<i64> upsertCurrentOpenItem(ProjectOpenItem item);

    // Current project
    std::shared_ptr<Project> currentProject() const { return m_currentProject; }
    void setCurrentProject(std::shared_ptr<Project> project) { m_currentProject = project; }

    // On-disk project directory (created in system temp for new projects)
    std::shared_ptr<ProjectDirectory> ensureProjectForModel(
        const std::string& modelName, const Path& modelSourcePath);
    std::shared_ptr<ProjectDirectory> currentDirectory() const { return m_currentDir; }

    // Promote a temporary project to permanent storage
    bool saveTemporaryProject();

    // Clean up temporary project directory (call on discard)
    void discardTemporaryProject();

    // Model operations within current project
    bool addModelToProject(i64 modelId);
    bool removeModelFromProject(i64 modelId);

  private:
    Path canonicalProjectDirectory(const Project& project) const;
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
