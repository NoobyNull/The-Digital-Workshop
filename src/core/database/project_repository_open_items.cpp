#include "project_repository.h"

#include "../utils/log.h"

namespace dw {

namespace {

std::string_view toDbString(ProjectOpenItemType type) {
    switch (type) {
    case ProjectOpenItemType::Model:
        return "model";
    case ProjectOpenItemType::Material:
        return "material";
    case ProjectOpenItemType::Stock:
        return "stock";
    case ProjectOpenItemType::Tool:
        return "tool";
    case ProjectOpenItemType::Operation:
        return "operation";
    case ProjectOpenItemType::Gcode:
        return "gcode";
    case ProjectOpenItemType::CutPlan:
        return "cut_plan";
    case ProjectOpenItemType::Cost:
        return "cost";
    case ProjectOpenItemType::Job:
        return "job";
    case ProjectOpenItemType::Labor:
        return "labor";
    case ProjectOpenItemType::Consumable:
        return "consumable";
    case ProjectOpenItemType::Zeroing:
        return "zeroing";
    }
    return "model";
}

ProjectOpenItemType openItemTypeFromDb(const std::string& value) {
    if (value == "material")
        return ProjectOpenItemType::Material;
    if (value == "stock")
        return ProjectOpenItemType::Stock;
    if (value == "tool")
        return ProjectOpenItemType::Tool;
    if (value == "operation")
        return ProjectOpenItemType::Operation;
    if (value == "gcode")
        return ProjectOpenItemType::Gcode;
    if (value == "cut_plan")
        return ProjectOpenItemType::CutPlan;
    if (value == "cost")
        return ProjectOpenItemType::Cost;
    if (value == "job")
        return ProjectOpenItemType::Job;
    if (value == "labor")
        return ProjectOpenItemType::Labor;
    if (value == "consumable")
        return ProjectOpenItemType::Consumable;
    if (value == "zeroing")
        return ProjectOpenItemType::Zeroing;
    return ProjectOpenItemType::Model;
}

std::string_view toDbString(ProjectOpenItemStatus status) {
    switch (status) {
    case ProjectOpenItemStatus::Planned:
        return "planned";
    case ProjectOpenItemStatus::Ready:
        return "ready";
    case ProjectOpenItemStatus::Generated:
        return "generated";
    case ProjectOpenItemStatus::Sent:
        return "sent";
    case ProjectOpenItemStatus::Complete:
        return "complete";
    case ProjectOpenItemStatus::Stale:
        return "stale";
    case ProjectOpenItemStatus::Missing:
        return "missing";
    }
    return "planned";
}

ProjectOpenItemStatus openItemStatusFromDb(const std::string& value) {
    if (value == "ready")
        return ProjectOpenItemStatus::Ready;
    if (value == "generated")
        return ProjectOpenItemStatus::Generated;
    if (value == "sent")
        return ProjectOpenItemStatus::Sent;
    if (value == "complete")
        return ProjectOpenItemStatus::Complete;
    if (value == "stale")
        return ProjectOpenItemStatus::Stale;
    if (value == "missing")
        return ProjectOpenItemStatus::Missing;
    return ProjectOpenItemStatus::Planned;
}

bool bindOptionalInt(Statement& stmt, int index, const std::optional<i64>& value) {
    return value.has_value() ? stmt.bindInt(index, *value) : stmt.bindNull(index);
}

} // namespace

std::optional<i64> ProjectRepository::insertOpenItem(const ProjectOpenItem& item) {
    auto stmt = m_db.prepare(R"(
        INSERT INTO project_open_items (
            project_id, item_type, source_table, source_id, source_key, parent_item_id,
            status, display_name, intent_json, snapshot_json
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    if (!stmt.isValid()) {
        return std::nullopt;
    }

    if (!stmt.bindInt(1, item.projectId) ||
        !stmt.bindText(2, std::string(toDbString(item.itemType))) ||
        !stmt.bindText(3, item.sourceTable) || !bindOptionalInt(stmt, 4, item.sourceId) ||
        !stmt.bindText(5, item.sourceKey) || !bindOptionalInt(stmt, 6, item.parentItemId) ||
        !stmt.bindText(7, std::string(toDbString(item.status))) ||
        !stmt.bindText(8, item.displayName) || !stmt.bindText(9, item.intentJson) ||
        !stmt.bindText(10, item.snapshotJson)) {
        return std::nullopt;
    }

    if (!stmt.execute()) {
        log::errorf("ProjectRepo",
                    "Failed to insert project open item: %s",
                    m_db.lastError().c_str());
        return std::nullopt;
    }

    updateModifiedTime(item.projectId);
    return m_db.lastInsertId();
}

std::optional<ProjectOpenItem> ProjectRepository::findOpenItemById(i64 id) {
    auto stmt = m_db.prepare(R"(
        SELECT id, project_id, item_type, source_table, source_id, source_key, parent_item_id,
               status, display_name, intent_json, snapshot_json, created_at, modified_at
        FROM project_open_items
        WHERE id = ?
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, id)) {
        return std::nullopt;
    }

    if (stmt.step()) {
        return rowToOpenItem(stmt);
    }

    return std::nullopt;
}

std::vector<ProjectOpenItem> ProjectRepository::listOpenItemsForProject(i64 projectId) {
    std::vector<ProjectOpenItem> results;
    auto stmt = m_db.prepare(R"(
        SELECT id, project_id, item_type, source_table, source_id, source_key, parent_item_id,
               status, display_name, intent_json, snapshot_json, created_at, modified_at
        FROM project_open_items
        WHERE project_id = ?
        ORDER BY CASE WHEN parent_item_id IS NULL THEN 0 ELSE 1 END,
                 COALESCE(parent_item_id, id), id
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, projectId)) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToOpenItem(stmt));
    }

    return results;
}

std::vector<ProjectOpenItem> ProjectRepository::findOpenItemsBySource(i64 projectId,
                                                                      std::string_view sourceTable,
                                                                      i64 sourceId) {
    std::vector<ProjectOpenItem> results;
    auto stmt = m_db.prepare(R"(
        SELECT id, project_id, item_type, source_table, source_id, source_key, parent_item_id,
               status, display_name, intent_json, snapshot_json, created_at, modified_at
        FROM project_open_items
        WHERE project_id = ? AND source_table = ? AND source_id = ?
        ORDER BY id
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, projectId) ||
        !stmt.bindText(2, std::string(sourceTable)) || !stmt.bindInt(3, sourceId)) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToOpenItem(stmt));
    }

    return results;
}

std::vector<ProjectOpenItem> ProjectRepository::findOpenItemsBySourceKey(
    i64 projectId, std::string_view sourceKey) {
    std::vector<ProjectOpenItem> results;
    auto stmt = m_db.prepare(R"(
        SELECT id, project_id, item_type, source_table, source_id, source_key, parent_item_id,
               status, display_name, intent_json, snapshot_json, created_at, modified_at
        FROM project_open_items
        WHERE project_id = ? AND source_key = ?
        ORDER BY id
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, projectId) ||
        !stmt.bindText(2, std::string(sourceKey))) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToOpenItem(stmt));
    }

    return results;
}

bool ProjectRepository::updateOpenItem(const ProjectOpenItem& item) {
    auto stmt = m_db.prepare(R"(
        UPDATE project_open_items SET
            item_type = ?,
            source_table = ?,
            source_id = ?,
            source_key = ?,
            parent_item_id = ?,
            status = ?,
            display_name = ?,
            intent_json = ?,
            snapshot_json = ?,
            modified_at = CURRENT_TIMESTAMP
        WHERE id = ? AND project_id = ?
    )");

    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, std::string(toDbString(item.itemType))) ||
        !stmt.bindText(2, item.sourceTable) || !bindOptionalInt(stmt, 3, item.sourceId) ||
        !stmt.bindText(4, item.sourceKey) || !bindOptionalInt(stmt, 5, item.parentItemId) ||
        !stmt.bindText(6, std::string(toDbString(item.status))) ||
        !stmt.bindText(7, item.displayName) || !stmt.bindText(8, item.intentJson) ||
        !stmt.bindText(9, item.snapshotJson) || !stmt.bindInt(10, item.id) ||
        !stmt.bindInt(11, item.projectId)) {
        return false;
    }

    bool result = stmt.execute();
    if (result) {
        updateModifiedTime(item.projectId);
    }
    return result;
}

std::optional<i64> ProjectRepository::upsertOpenItemBySource(const ProjectOpenItem& item) {
    if (item.projectId <= 0 || item.sourceTable.empty() || !item.sourceId.has_value()) {
        return std::nullopt;
    }

    auto existingItems = findOpenItemsBySource(item.projectId, item.sourceTable, *item.sourceId);
    if (existingItems.empty()) {
        return insertOpenItem(item);
    }

    auto updated = item;
    updated.id = existingItems.front().id;
    if (!updateOpenItem(updated)) {
        return std::nullopt;
    }

    return updated.id;
}

std::optional<i64> ProjectRepository::upsertOpenItemBySourceKey(const ProjectOpenItem& item) {
    if (item.projectId <= 0 || item.sourceKey.empty()) {
        return std::nullopt;
    }

    auto existingItems = findOpenItemsBySourceKey(item.projectId, item.sourceKey);
    if (existingItems.empty()) {
        return insertOpenItem(item);
    }

    auto updated = item;
    updated.id = existingItems.front().id;
    if (!updateOpenItem(updated)) {
        return std::nullopt;
    }

    return updated.id;
}

bool ProjectRepository::removeOpenItem(i64 id) {
    auto existing = findOpenItemById(id);
    if (!existing.has_value()) {
        return false;
    }

    auto stmt = m_db.prepare("DELETE FROM project_open_items WHERE id = ?");
    if (!stmt.isValid() || !stmt.bindInt(1, id)) {
        return false;
    }

    bool result = stmt.execute();
    if (result) {
        updateModifiedTime(existing->projectId);
    }
    return result;
}
ProjectOpenItem ProjectRepository::rowToOpenItem(Statement& stmt) {
    ProjectOpenItem item;
    item.id = stmt.getInt(0);
    item.projectId = stmt.getInt(1);
    item.itemType = openItemTypeFromDb(stmt.getText(2));
    item.sourceTable = stmt.getText(3);
    if (!stmt.isNull(4)) {
        item.sourceId = stmt.getInt(4);
    }
    item.sourceKey = stmt.getText(5);
    if (!stmt.isNull(6)) {
        item.parentItemId = stmt.getInt(6);
    }
    item.status = openItemStatusFromDb(stmt.getText(7));
    item.displayName = stmt.getText(8);
    item.intentJson = stmt.getText(9);
    item.snapshotJson = stmt.getText(10);
    item.createdAt = stmt.getText(11);
    item.modifiedAt = stmt.getText(12);
    return item;
}

} // namespace dw
