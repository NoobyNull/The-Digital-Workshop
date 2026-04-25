#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../types.h"
#include "database.h"

namespace dw {

// Project data structure
struct ProjectRecord {
    i64 id = 0;
    std::string name;
    std::string description;
    Path filePath;
    std::string notes;
    std::string createdAt;
    std::string modifiedAt;
};

// Project-Model link
struct ProjectModelLink {
    i64 projectId = 0;
    i64 modelId = 0;
    int sortOrder = 0;
    std::string addedAt;
};

enum class ProjectOpenItemType {
    Model,
    Material,
    Stock,
    Tool,
    Operation,
    Gcode,
    CutPlan,
    Cost,
    Job,
    Labor,
    Consumable,
};

enum class ProjectOpenItemStatus {
    Planned,
    Ready,
    Generated,
    Sent,
    Complete,
    Stale,
    Missing,
};

struct ProjectOpenItem {
    i64 id = 0;
    i64 projectId = 0;
    ProjectOpenItemType itemType = ProjectOpenItemType::Model;
    std::string sourceTable;
    std::optional<i64> sourceId;
    std::string sourceKey;
    std::optional<i64> parentItemId;
    ProjectOpenItemStatus status = ProjectOpenItemStatus::Planned;
    std::string displayName;
    std::string intentJson = "{}";
    std::string snapshotJson = "{}";
    std::string createdAt;
    std::string modifiedAt;
};

// Repository for project CRUD operations
class ProjectRepository {
  public:
    explicit ProjectRepository(Database& db);

    // Create
    std::optional<i64> insert(const ProjectRecord& project);

    // Read
    std::optional<ProjectRecord> findById(i64 id);
    std::vector<ProjectRecord> findAll();
    std::vector<ProjectRecord> findByName(std::string_view searchTerm);

    // Update
    bool update(const ProjectRecord& project);
    bool updateModifiedTime(i64 id);

    // Delete
    bool remove(i64 id);

    // Project-Model relationships
    bool addModel(i64 projectId, i64 modelId, int sortOrder = -1);
    bool removeModel(i64 projectId, i64 modelId);
    bool updateModelOrder(i64 projectId, i64 modelId, int sortOrder);
    std::vector<i64> getModelIds(i64 projectId);
    std::vector<i64> getProjectsForModel(i64 modelId);
    bool hasModel(i64 projectId, i64 modelId);

    // Project open items
    std::optional<i64> insertOpenItem(const ProjectOpenItem& item);
    std::optional<ProjectOpenItem> findOpenItemById(i64 id);
    std::vector<ProjectOpenItem> listOpenItemsForProject(i64 projectId);
    std::vector<ProjectOpenItem> findOpenItemsBySource(i64 projectId,
                                                       std::string_view sourceTable,
                                                       i64 sourceId);
    std::vector<ProjectOpenItem> findOpenItemsBySourceKey(i64 projectId,
                                                          std::string_view sourceKey);
    bool updateOpenItem(const ProjectOpenItem& item);
    bool removeOpenItem(i64 id);
    int ensureOpenItemsForProject(i64 projectId);
    int validateOpenItemsForProject(i64 projectId);

    // Utility
    i64 count();

  private:
    ProjectRecord rowToProject(Statement& stmt);
    ProjectOpenItem rowToOpenItem(Statement& stmt);

    Database& m_db;
};

} // namespace dw
