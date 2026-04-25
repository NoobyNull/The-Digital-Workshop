#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../types.h"
#include "database.h"

namespace dw {

// G-code file data structure
struct GCodeRecord {
    i64 id = 0;
    std::string hash;
    std::string name;
    Path filePath;
    u64 fileSize = 0;
    Vec3 boundsMin;
    Vec3 boundsMax;
    f32 totalDistance = 0.0f;
    f32 estimatedTime = 0.0f;
    std::vector<f32> feedRates;
    std::vector<int> toolNumbers;
    std::string importedAt;
    Path thumbnailPath;
};

// Operation group data structure
struct OperationGroup {
    i64 id = 0;
    i64 modelId = 0;
    std::string name;
    int sortOrder = 0;
};

// G-code template data structure
struct GCodeTemplate {
    i64 id = 0;
    std::string name;
    std::vector<std::string> groups;
};

// Repository for G-code file CRUD operations
class GCodeRepository {
  public:
    explicit GCodeRepository(Database& db);

    // G-code file CRUD
    std::optional<i64> insert(const GCodeRecord& record);
    std::optional<GCodeRecord> findById(i64 id);
    std::optional<GCodeRecord> findByHash(std::string_view hash);
    std::optional<GCodeRecord> findByPath(const Path& filePath);
    std::vector<GCodeRecord> findAll();
    std::vector<GCodeRecord> findByName(std::string_view searchTerm);
    bool update(const GCodeRecord& record);
    bool updateThumbnail(i64 id, const Path& thumbnailPath);
    bool remove(i64 id);
    bool exists(std::string_view hash);
    i64 count();

    // Hierarchy operations
    std::optional<i64> createGroup(i64 modelId, const std::string& name, int sortOrder);
    std::vector<OperationGroup> getGroups(i64 modelId);
    bool addToGroup(i64 groupId, i64 gcodeId, int sortOrder = -1);
    bool removeFromGroup(i64 groupId, i64 gcodeId);
    std::vector<GCodeRecord> getGroupMembers(i64 groupId);
    bool deleteGroup(i64 groupId);

    // Project association
    bool addToProject(i64 projectId, i64 gcodeId, int sortOrder = -1);
    bool removeFromProject(i64 projectId, i64 gcodeId);
    std::vector<GCodeRecord> findByProject(i64 projectId);
    std::vector<i64> getProjectsForGCode(i64 gcodeId);
    bool isInProject(i64 projectId, i64 gcodeId);

    // Template operations
    std::vector<GCodeTemplate> getTemplates();
    bool applyTemplate(i64 modelId, const std::string& templateName);

  private:
    GCodeRecord rowToGCode(Statement& stmt);
    std::string feedRatesToJson(const std::vector<f32>& feedRates);
    std::vector<f32> jsonToFeedRates(const std::string& json);
    std::string toolNumbersToJson(const std::vector<int>& toolNumbers);
    std::vector<int> jsonToToolNumbers(const std::string& json);
    std::vector<std::string> jsonToGroups(const std::string& json);

    Database& m_db;
};

} // namespace dw
