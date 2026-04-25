#include "project_repository.h"

#include <cmath>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace dw {

namespace {

std::string_view toDbString(ProjectOpenItemType type) {
    switch (type) {
    case ProjectOpenItemType::Model: return "model";
    case ProjectOpenItemType::Material: return "material";
    case ProjectOpenItemType::Stock: return "stock";
    case ProjectOpenItemType::Tool: return "tool";
    case ProjectOpenItemType::Operation: return "operation";
    case ProjectOpenItemType::Gcode: return "gcode";
    case ProjectOpenItemType::CutPlan: return "cut_plan";
    case ProjectOpenItemType::Cost: return "cost";
    case ProjectOpenItemType::Job: return "job";
    case ProjectOpenItemType::Labor: return "labor";
    case ProjectOpenItemType::Consumable: return "consumable";
    }
    return "model";
}

ProjectOpenItemType openItemTypeFromDb(const std::string& value) {
    if (value == "material") return ProjectOpenItemType::Material;
    if (value == "stock") return ProjectOpenItemType::Stock;
    if (value == "tool") return ProjectOpenItemType::Tool;
    if (value == "operation") return ProjectOpenItemType::Operation;
    if (value == "gcode") return ProjectOpenItemType::Gcode;
    if (value == "cut_plan") return ProjectOpenItemType::CutPlan;
    if (value == "cost") return ProjectOpenItemType::Cost;
    if (value == "job") return ProjectOpenItemType::Job;
    if (value == "labor") return ProjectOpenItemType::Labor;
    if (value == "consumable") return ProjectOpenItemType::Consumable;
    return ProjectOpenItemType::Model;
}

std::string_view toDbString(ProjectOpenItemStatus status) {
    switch (status) {
    case ProjectOpenItemStatus::Planned: return "planned";
    case ProjectOpenItemStatus::Ready: return "ready";
    case ProjectOpenItemStatus::Generated: return "generated";
    case ProjectOpenItemStatus::Sent: return "sent";
    case ProjectOpenItemStatus::Complete: return "complete";
    case ProjectOpenItemStatus::Stale: return "stale";
    case ProjectOpenItemStatus::Missing: return "missing";
    }
    return "planned";
}

ProjectOpenItemStatus openItemStatusFromDb(const std::string& value) {
    if (value == "ready") return ProjectOpenItemStatus::Ready;
    if (value == "generated") return ProjectOpenItemStatus::Generated;
    if (value == "sent") return ProjectOpenItemStatus::Sent;
    if (value == "complete") return ProjectOpenItemStatus::Complete;
    if (value == "stale") return ProjectOpenItemStatus::Stale;
    if (value == "missing") return ProjectOpenItemStatus::Missing;
    return ProjectOpenItemStatus::Planned;
}

bool bindOptionalInt(Statement& stmt, int index, const std::optional<i64>& value) {
    return value.has_value() ? stmt.bindInt(index, *value) : stmt.bindNull(index);
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    return out.str();
}

nlohmann::json parseSnapshot(const std::string& snapshot) {
    auto parsed = nlohmann::json::parse(snapshot, nullptr, false);
    return parsed.is_object() ? parsed : nlohmann::json::object();
}

bool doubleChanged(const nlohmann::json& snapshot, std::string_view key, double current) {
    if (!snapshot.contains(key)) {
        return false;
    }
    return std::abs(snapshot.value(std::string(key), current) - current) > 0.0001;
}

} // namespace

ProjectRepository::ProjectRepository(Database& db) : m_db(db) {}

std::optional<i64> ProjectRepository::insert(const ProjectRecord& project) {
    auto stmt = m_db.prepare(R"(
        INSERT INTO projects (name, description, file_path, notes)
        VALUES (?, ?, ?, ?)
    )");

    if (!stmt.isValid()) {
        return std::nullopt;
    }

    if (!stmt.bindText(1, project.name) || !stmt.bindText(2, project.description) ||
        !stmt.bindText(3, project.filePath.string()) || !stmt.bindText(4, project.notes)) {
        log::error("ProjectRepo", "Failed to bind insert parameters");
        return std::nullopt;
    }

    if (!stmt.execute()) {
        log::errorf("ProjectRepo", "Failed to insert project: %s", m_db.lastError().c_str());
        return std::nullopt;
    }

    return m_db.lastInsertId();
}

std::optional<ProjectRecord> ProjectRepository::findById(i64 id) {
    auto stmt = m_db.prepare("SELECT * FROM projects WHERE id = ?");
    if (!stmt.isValid()) {
        return std::nullopt;
    }

    if (!stmt.bindInt(1, id)) {
        return std::nullopt;
    }

    if (stmt.step()) {
        return rowToProject(stmt);
    }

    return std::nullopt;
}

std::vector<ProjectRecord> ProjectRepository::findAll() {
    std::vector<ProjectRecord> results;

    auto stmt = m_db.prepare("SELECT * FROM projects ORDER BY modified_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToProject(stmt));
    }

    return results;
}

std::vector<ProjectRecord> ProjectRepository::findByName(std::string_view searchTerm) {
    std::vector<ProjectRecord> results;

    auto stmt = m_db.prepare(
        "SELECT * FROM projects WHERE name LIKE ? ESCAPE '\\' ORDER BY modified_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    if (!stmt.bindText(1, "%" + str::escapeLike(searchTerm) + "%")) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToProject(stmt));
    }

    return results;
}

bool ProjectRepository::update(const ProjectRecord& project) {
    auto stmt = m_db.prepare(R"(
        UPDATE projects SET
            name = ?,
            description = ?,
            file_path = ?,
            notes = ?,
            modified_at = CURRENT_TIMESTAMP
        WHERE id = ?
    )");

    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, project.name) || !stmt.bindText(2, project.description) ||
        !stmt.bindText(3, project.filePath.string()) || !stmt.bindText(4, project.notes) ||
        !stmt.bindInt(5, project.id)) {
        return false;
    }

    return stmt.execute();
}

bool ProjectRepository::updateModifiedTime(i64 id) {
    auto stmt = m_db.prepare("UPDATE projects SET modified_at = CURRENT_TIMESTAMP WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, id)) {
        return false;
    }
    return stmt.execute();
}

bool ProjectRepository::remove(i64 id) {
    auto stmt = m_db.prepare("DELETE FROM projects WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, id)) {
        return false;
    }
    return stmt.execute();
}

bool ProjectRepository::addModel(i64 projectId, i64 modelId, int sortOrder) {
    if (sortOrder < 0) {
        auto orderStmt =
            m_db.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM project_models "
                         "WHERE project_id = ?");
        if (orderStmt.isValid() && orderStmt.bindInt(1, projectId) && orderStmt.step()) {
            sortOrder = static_cast<int>(orderStmt.getInt(0));
        } else {
            sortOrder = 0;
        }
    }

    auto stmt = m_db.prepare(R"(
        INSERT OR REPLACE INTO project_models (project_id, model_id, sort_order)
        VALUES (?, ?, ?)
    )");

    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, projectId) || !stmt.bindInt(2, modelId) || !stmt.bindInt(3, sortOrder)) {
        return false;
    }

    bool result = stmt.execute();
    if (result) {
        updateModifiedTime(projectId);
    }
    return result;
}

bool ProjectRepository::removeModel(i64 projectId, i64 modelId) {
    auto stmt = m_db.prepare("DELETE FROM project_models WHERE project_id = ? AND model_id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, projectId) || !stmt.bindInt(2, modelId)) {
        return false;
    }

    bool result = stmt.execute();
    if (result) {
        updateModifiedTime(projectId);
    }
    return result;
}

bool ProjectRepository::updateModelOrder(i64 projectId, i64 modelId, int sortOrder) {
    auto stmt = m_db.prepare(R"(
        UPDATE project_models SET sort_order = ?
        WHERE project_id = ? AND model_id = ?
    )");

    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, sortOrder) || !stmt.bindInt(2, projectId) || !stmt.bindInt(3, modelId)) {
        return false;
    }

    return stmt.execute();
}

std::vector<i64> ProjectRepository::getModelIds(i64 projectId) {
    std::vector<i64> ids;

    auto stmt = m_db.prepare(
        "SELECT model_id FROM project_models WHERE project_id = ? ORDER BY sort_order");
    if (!stmt.isValid()) {
        return ids;
    }

    if (!stmt.bindInt(1, projectId)) {
        return ids;
    }

    while (stmt.step()) {
        ids.push_back(stmt.getInt(0));
    }

    return ids;
}

std::vector<i64> ProjectRepository::getProjectsForModel(i64 modelId) {
    std::vector<i64> ids;

    auto stmt = m_db.prepare("SELECT project_id FROM project_models WHERE model_id = ?");
    if (!stmt.isValid()) {
        return ids;
    }

    if (!stmt.bindInt(1, modelId)) {
        return ids;
    }

    while (stmt.step()) {
        ids.push_back(stmt.getInt(0));
    }

    return ids;
}

bool ProjectRepository::hasModel(i64 projectId, i64 modelId) {
    auto stmt =
        m_db.prepare("SELECT 1 FROM project_models WHERE project_id = ? AND model_id = ? LIMIT 1");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, projectId) || !stmt.bindInt(2, modelId)) {
        return false;
    }

    return stmt.step();
}

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

    if (!stmt.bindInt(1, item.projectId) || !stmt.bindText(2, std::string(toDbString(item.itemType))) ||
        !stmt.bindText(3, item.sourceTable) || !bindOptionalInt(stmt, 4, item.sourceId) ||
        !stmt.bindText(5, item.sourceKey) || !bindOptionalInt(stmt, 6, item.parentItemId) ||
        !stmt.bindText(7, std::string(toDbString(item.status))) ||
        !stmt.bindText(8, item.displayName) || !stmt.bindText(9, item.intentJson) ||
        !stmt.bindText(10, item.snapshotJson)) {
        return std::nullopt;
    }

    if (!stmt.execute()) {
        log::errorf("ProjectRepo", "Failed to insert project open item: %s",
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

std::vector<ProjectOpenItem>
ProjectRepository::findOpenItemsBySourceKey(i64 projectId, std::string_view sourceKey) {
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

int ProjectRepository::ensureOpenItemsForProject(i64 projectId) {
    int created = 0;

    auto pruneModels = m_db.prepare(R"(
        DELETE FROM project_open_items
        WHERE project_id = ?
          AND item_type = 'model'
          AND source_table = 'models'
          AND source_id NOT IN (
              SELECT model_id FROM project_models WHERE project_id = ?
          )
    )");
    if (pruneModels.isValid() && pruneModels.bindInt(1, projectId) &&
        pruneModels.bindInt(2, projectId)) {
        (void)pruneModels.execute();
    }

    auto pruneGcode = m_db.prepare(R"(
        DELETE FROM project_open_items
        WHERE project_id = ?
          AND item_type = 'gcode'
          AND source_table = 'gcode_files'
          AND source_id NOT IN (
              SELECT gcode_id FROM project_gcode WHERE project_id = ?
          )
    )");
    if (pruneGcode.isValid() && pruneGcode.bindInt(1, projectId) &&
        pruneGcode.bindInt(2, projectId)) {
        (void)pruneGcode.execute();
    }

    auto pruneJobs = m_db.prepare(R"(
        DELETE FROM project_open_items
        WHERE project_id = ?
          AND item_type = 'job'
          AND source_table = 'cnc_jobs'
          AND source_id NOT IN (
              SELECT j.id
              FROM cnc_jobs j
              INNER JOIN gcode_files g ON g.file_path = j.file_path
              INNER JOIN project_gcode pg ON pg.gcode_id = g.id
              WHERE pg.project_id = ?
          )
    )");
    if (pruneJobs.isValid() && pruneJobs.bindInt(1, projectId) &&
        pruneJobs.bindInt(2, projectId)) {
        (void)pruneJobs.execute();
    }

    auto pruneCutPlans = m_db.prepare(R"(
        DELETE FROM project_open_items
        WHERE project_id = ?
          AND item_type = 'cut_plan'
          AND source_table = 'cut_plans'
          AND source_id NOT IN (
              SELECT id FROM cut_plans WHERE project_id = ?
          )
    )");
    if (pruneCutPlans.isValid() && pruneCutPlans.bindInt(1, projectId) &&
        pruneCutPlans.bindInt(2, projectId)) {
        (void)pruneCutPlans.execute();
    }

    auto pruneCosts = m_db.prepare(R"(
        DELETE FROM project_open_items
        WHERE project_id = ?
          AND item_type = 'cost'
          AND source_table = 'costing_records'
          AND source_id NOT IN (
              SELECT id FROM costing_records WHERE project_id = ?
          )
    )");
    if (pruneCosts.isValid() && pruneCosts.bindInt(1, projectId) &&
        pruneCosts.bindInt(2, projectId)) {
        (void)pruneCosts.execute();
    }

    auto pruneStock = m_db.prepare(R"(
        DELETE FROM project_open_items
        WHERE project_id = ?
          AND item_type = 'stock'
          AND source_table = 'stock_sizes'
          AND source_id NOT IN (
              SELECT s.id
              FROM stock_sizes s
              INNER JOIN models m ON m.material_id = s.material_id
              INNER JOIN project_models pm ON pm.model_id = m.id
              WHERE pm.project_id = ?
          )
    )");
    if (pruneStock.isValid() && pruneStock.bindInt(1, projectId) &&
        pruneStock.bindInt(2, projectId)) {
        (void)pruneStock.execute();
    }

    auto modelStmt = m_db.prepare(R"(
        SELECT m.id, m.name, m.hash, m.file_path, m.file_format,
               m.bounds_min_x, m.bounds_min_y, m.bounds_min_z,
               m.bounds_max_x, m.bounds_max_y, m.bounds_max_z
        FROM models m
        INNER JOIN project_models pm ON pm.model_id = m.id
        WHERE pm.project_id = ?
        ORDER BY pm.sort_order
    )");
    if (modelStmt.isValid() && modelStmt.bindInt(1, projectId)) {
        while (modelStmt.step()) {
            i64 modelId = modelStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "models", modelId).empty()) {
                continue;
            }

            std::ostringstream snapshot;
            snapshot << "{\"hash\":\"" << jsonEscape(modelStmt.getText(2)) << "\","
                     << "\"file_path\":\"" << jsonEscape(modelStmt.getText(3)) << "\","
                     << "\"file_format\":\"" << jsonEscape(modelStmt.getText(4)) << "\","
                     << "\"bounds\":{\"min\":[" << modelStmt.getDouble(5) << ","
                     << modelStmt.getDouble(6) << "," << modelStmt.getDouble(7)
                     << "],\"max\":[" << modelStmt.getDouble(8) << ","
                     << modelStmt.getDouble(9) << "," << modelStmt.getDouble(10) << "]}}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Model;
            item.sourceTable = "models";
            item.sourceId = modelId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = modelStmt.getText(1);
            item.intentJson = R"({"role":"project_model"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto materialStmt = m_db.prepare(R"(
        SELECT DISTINCT mat.id, mat.name, mat.category, mat.janka_hardness,
               mat.grain_direction_deg, mat.archive_path, mat.thumbnail_path
        FROM materials mat
        INNER JOIN models m ON m.material_id = mat.id
        INNER JOIN project_models pm ON pm.model_id = m.id
        WHERE pm.project_id = ?
        ORDER BY mat.name
    )");
    if (materialStmt.isValid() && materialStmt.bindInt(1, projectId)) {
        while (materialStmt.step()) {
            i64 materialId = materialStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "materials", materialId).empty()) {
                continue;
            }

            std::ostringstream snapshot;
            snapshot << "{\"name\":\"" << jsonEscape(materialStmt.getText(1)) << "\","
                     << "\"category\":\"" << jsonEscape(materialStmt.getText(2)) << "\","
                     << "\"janka_hardness\":" << materialStmt.getDouble(3) << ","
                     << "\"grain_direction_deg\":" << materialStmt.getDouble(4) << ","
                     << "\"archive_path\":\"" << jsonEscape(materialStmt.getText(5)) << "\","
                     << "\"thumbnail_path\":\"" << jsonEscape(materialStmt.getText(6))
                     << "\"}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Material;
            item.sourceTable = "materials";
            item.sourceId = materialId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = materialStmt.getText(1);
            item.intentJson = R"({"selected_use":"model_material"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto stockStmt = m_db.prepare(R"(
        SELECT DISTINCT s.id, s.material_id, s.name, s.width_mm, s.height_mm,
               s.thickness_mm, s.price_per_unit, s.unit_label, mat.name
        FROM stock_sizes s
        INNER JOIN materials mat ON mat.id = s.material_id
        INNER JOIN models m ON m.material_id = mat.id
        INNER JOIN project_models pm ON pm.model_id = m.id
        WHERE pm.project_id = ?
        ORDER BY mat.name, s.name
    )");
    if (stockStmt.isValid() && stockStmt.bindInt(1, projectId)) {
        while (stockStmt.step()) {
            i64 stockId = stockStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "stock_sizes", stockId).empty()) {
                continue;
            }

            std::string stockName = stockStmt.getText(2);
            std::string materialName = stockStmt.getText(8);
            std::string displayName = stockName.empty() ? materialName + " stock" : stockName;

            std::ostringstream snapshot;
            snapshot << "{\"material_id\":" << stockStmt.getInt(1)
                     << ",\"material_name\":\"" << jsonEscape(materialName) << "\","
                     << "\"stock_name\":\"" << jsonEscape(stockName) << "\","
                     << "\"width_mm\":" << stockStmt.getDouble(3)
                     << ",\"height_mm\":" << stockStmt.getDouble(4)
                     << ",\"thickness_mm\":" << stockStmt.getDouble(5)
                     << ",\"price_per_unit\":" << stockStmt.getDouble(6)
                     << ",\"unit_label\":\"" << jsonEscape(stockStmt.getText(7)) << "\"}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Stock;
            item.sourceTable = "stock_sizes";
            item.sourceId = stockId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = displayName;
            item.intentJson = R"({"selected_use":"model_material_stock"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto operationStmt = m_db.prepare(R"(
        SELECT DISTINCT og.id, og.model_id, og.name
        FROM operation_groups og
        INNER JOIN gcode_group_members gm ON gm.group_id = og.id
        INNER JOIN project_gcode pg ON pg.gcode_id = gm.gcode_id
        WHERE pg.project_id = ?
        ORDER BY og.model_id, og.sort_order, og.id
    )");
    if (operationStmt.isValid() && operationStmt.bindInt(1, projectId)) {
        while (operationStmt.step()) {
            i64 groupId = operationStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "operation_groups", groupId).empty()) {
                continue;
            }

            std::optional<i64> parentItemId;
            auto modelItems = findOpenItemsBySource(projectId, "models", operationStmt.getInt(1));
            if (!modelItems.empty()) {
                parentItemId = modelItems.front().id;
            }

            std::ostringstream snapshot;
            snapshot << "{\"operation_group_id\":" << groupId << ","
                     << "\"model_id\":" << operationStmt.getInt(1) << ","
                     << "\"name\":\"" << jsonEscape(operationStmt.getText(2)) << "\"}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Operation;
            item.sourceTable = "operation_groups";
            item.sourceId = groupId;
            item.parentItemId = parentItemId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = operationStmt.getText(2);
            item.intentJson = R"({"operation_kind":"direct_carve"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto gcodeStmt = m_db.prepare(R"(
        SELECT g.id, g.name, g.hash, g.file_path, g.file_size, g.estimated_time,
               g.feed_rates, g.tool_numbers
        FROM gcode_files g
        INNER JOIN project_gcode pg ON pg.gcode_id = g.id
        WHERE pg.project_id = ?
        ORDER BY pg.sort_order
    )");
    if (gcodeStmt.isValid() && gcodeStmt.bindInt(1, projectId)) {
        while (gcodeStmt.step()) {
            i64 gcodeId = gcodeStmt.getInt(0);
            std::optional<i64> parentItemId;
            auto parentStmt = m_db.prepare(R"(
                SELECT og.id
                FROM operation_groups og
                INNER JOIN gcode_group_members gm ON gm.group_id = og.id
                WHERE gm.gcode_id = ?
                ORDER BY og.id
                LIMIT 1
            )");
            if (parentStmt.isValid() && parentStmt.bindInt(1, gcodeId) && parentStmt.step()) {
                auto operationItems =
                    findOpenItemsBySource(projectId, "operation_groups", parentStmt.getInt(0));
                if (!operationItems.empty()) {
                    parentItemId = operationItems.front().id;
                }
            }

            auto existingItems = findOpenItemsBySource(projectId, "gcode_files", gcodeId);
            if (!existingItems.empty()) {
                if (parentItemId.has_value() && existingItems.front().parentItemId != parentItemId) {
                    auto updated = existingItems.front();
                    updated.parentItemId = parentItemId;
                    (void)updateOpenItem(updated);
                }
                continue;
            }

            std::ostringstream snapshot;
            snapshot << "{\"hash\":\"" << jsonEscape(gcodeStmt.getText(2)) << "\","
                     << "\"file_path\":\"" << jsonEscape(gcodeStmt.getText(3)) << "\","
                     << "\"file_size\":" << gcodeStmt.getInt(4) << ","
                     << "\"estimated_time\":" << gcodeStmt.getDouble(5) << ","
                     << "\"feed_rates\":" << gcodeStmt.getText(6) << ","
                     << "\"tool_numbers\":" << gcodeStmt.getText(7) << "}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Gcode;
            item.sourceTable = "gcode_files";
            item.sourceId = gcodeId;
            item.parentItemId = parentItemId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = gcodeStmt.getText(1);
            item.intentJson = R"({"role":"sendable_program"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto toolStmt = m_db.prepare(R"(
        SELECT g.id, g.tool_numbers
        FROM gcode_files g
        INNER JOIN project_gcode pg ON pg.gcode_id = g.id
        WHERE pg.project_id = ?
        ORDER BY pg.sort_order
    )");
    if (toolStmt.isValid() && toolStmt.bindInt(1, projectId)) {
        while (toolStmt.step()) {
            i64 gcodeId = toolStmt.getInt(0);
            auto toolJson = nlohmann::json::parse(toolStmt.getText(1), nullptr, false);
            if (!toolJson.is_array()) {
                continue;
            }

            std::optional<i64> parentItemId;
            auto parentStmt = m_db.prepare(R"(
                SELECT og.id
                FROM operation_groups og
                INNER JOIN gcode_group_members gm ON gm.group_id = og.id
                WHERE gm.gcode_id = ?
                ORDER BY og.id
                LIMIT 1
            )");
            if (parentStmt.isValid() && parentStmt.bindInt(1, gcodeId) && parentStmt.step()) {
                auto operationItems =
                    findOpenItemsBySource(projectId, "operation_groups", parentStmt.getInt(0));
                if (!operationItems.empty()) {
                    parentItemId = operationItems.front().id;
                }
            }
            if (!parentItemId.has_value()) {
                auto gcodeItems = findOpenItemsBySource(projectId, "gcode_files", gcodeId);
                if (!gcodeItems.empty()) {
                    parentItemId = gcodeItems.front().id;
                }
            }

            std::set<int> toolNumbers;
            for (const auto& value : toolJson) {
                if (value.is_number_integer()) {
                    toolNumbers.insert(value.get<int>());
                }
            }

            for (int toolNumber : toolNumbers) {
                std::string sourceKey = "gcode_files:" + std::to_string(gcodeId) +
                                        ":tool:" + std::to_string(toolNumber);
                auto existingItems = findOpenItemsBySourceKey(projectId, sourceKey);
                if (!existingItems.empty()) {
                    if (parentItemId.has_value() &&
                        existingItems.front().parentItemId != parentItemId) {
                        auto updated = existingItems.front();
                        updated.parentItemId = parentItemId;
                        (void)updateOpenItem(updated);
                    }
                    continue;
                }

                std::ostringstream snapshot;
                snapshot << "{\"gcode_id\":" << gcodeId
                         << ",\"tool_number\":" << toolNumber << "}";

                ProjectOpenItem item;
                item.projectId = projectId;
                item.itemType = ProjectOpenItemType::Tool;
                item.sourceKey = sourceKey;
                item.parentItemId = parentItemId;
                item.status = ProjectOpenItemStatus::Ready;
                item.displayName = "Tool " + std::to_string(toolNumber);
                item.intentJson = R"({"role":"required_by_gcode"})";
                item.snapshotJson = snapshot.str();
                if (insertOpenItem(item).has_value()) {
                    ++created;
                }
            }
        }
    }

    auto jobStmt = m_db.prepare(R"(
        SELECT DISTINCT j.id, j.file_name, j.file_path, j.total_lines, j.last_acked_line,
               j.status, j.error_count, j.elapsed_seconds, j.started_at, j.ended_at, g.id
        FROM cnc_jobs j
        INNER JOIN gcode_files g ON g.file_path = j.file_path
        INNER JOIN project_gcode pg ON pg.gcode_id = g.id
        WHERE pg.project_id = ?
        ORDER BY j.started_at, j.id
    )");
    if (jobStmt.isValid() && jobStmt.bindInt(1, projectId)) {
        while (jobStmt.step()) {
            i64 jobId = jobStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "cnc_jobs", jobId).empty()) {
                continue;
            }

            std::optional<i64> parentItemId;
            auto gcodeItems = findOpenItemsBySource(projectId, "gcode_files", jobStmt.getInt(10));
            if (!gcodeItems.empty()) {
                parentItemId = gcodeItems.front().id;
            }

            std::string jobStatus = jobStmt.getText(5);
            ProjectOpenItemStatus itemStatus = ProjectOpenItemStatus::Ready;
            if (jobStatus == "running") {
                itemStatus = ProjectOpenItemStatus::Sent;
            } else if (jobStatus == "completed") {
                itemStatus = ProjectOpenItemStatus::Complete;
            } else if (jobStatus == "aborted" || jobStatus == "interrupted") {
                itemStatus = ProjectOpenItemStatus::Stale;
            }

            std::ostringstream snapshot;
            snapshot << "{\"file_name\":\"" << jsonEscape(jobStmt.getText(1)) << "\","
                     << "\"file_path\":\"" << jsonEscape(jobStmt.getText(2)) << "\","
                     << "\"total_lines\":" << jobStmt.getInt(3)
                     << ",\"last_acked_line\":" << jobStmt.getInt(4)
                     << ",\"job_status\":\"" << jsonEscape(jobStatus) << "\","
                     << "\"error_count\":" << jobStmt.getInt(6)
                     << ",\"elapsed_seconds\":" << jobStmt.getDouble(7)
                     << ",\"started_at\":\"" << jsonEscape(jobStmt.getText(8)) << "\","
                     << "\"ended_at\":\"" << jsonEscape(jobStmt.getText(9)) << "\"}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Job;
            item.sourceTable = "cnc_jobs";
            item.sourceId = jobId;
            item.parentItemId = parentItemId;
            item.status = itemStatus;
            item.displayName = "Run: " + jobStmt.getText(1);
            item.intentJson = R"({"actual_type":"cnc_run"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto cutPlanStmt = m_db.prepare(R"(
        SELECT id, name, algorithm, allow_rotation, kerf, margin, sheets_used, efficiency
        FROM cut_plans
        WHERE project_id = ?
        ORDER BY created_at, id
    )");
    if (cutPlanStmt.isValid() && cutPlanStmt.bindInt(1, projectId)) {
        while (cutPlanStmt.step()) {
            i64 planId = cutPlanStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "cut_plans", planId).empty()) {
                continue;
            }

            std::ostringstream snapshot;
            snapshot << "{\"algorithm\":\"" << jsonEscape(cutPlanStmt.getText(2)) << "\","
                     << "\"allow_rotation\":" << (cutPlanStmt.getInt(3) != 0 ? "true" : "false")
                     << ",\"kerf\":" << cutPlanStmt.getDouble(4)
                     << ",\"margin\":" << cutPlanStmt.getDouble(5)
                     << ",\"sheets_used\":" << cutPlanStmt.getInt(6)
                     << ",\"efficiency\":" << cutPlanStmt.getDouble(7) << "}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::CutPlan;
            item.sourceTable = "cut_plans";
            item.sourceId = planId;
            item.status = ProjectOpenItemStatus::Generated;
            item.displayName = cutPlanStmt.getText(1);
            item.intentJson = R"({"selected_use":"cut_optimizer_plan"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    auto costStmt = m_db.prepare(R"(
        SELECT id, name, subtotal, tax_amount, discount_amount, total
        FROM costing_records
        WHERE project_id = ?
        ORDER BY created_at, id
    )");
    if (costStmt.isValid() && costStmt.bindInt(1, projectId)) {
        while (costStmt.step()) {
            i64 costId = costStmt.getInt(0);
            if (!findOpenItemsBySource(projectId, "costing_records", costId).empty()) {
                continue;
            }

            std::ostringstream snapshot;
            snapshot << "{\"subtotal\":" << costStmt.getDouble(2)
                     << ",\"tax_amount\":" << costStmt.getDouble(3)
                     << ",\"discount_amount\":" << costStmt.getDouble(4)
                     << ",\"total\":" << costStmt.getDouble(5) << "}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Cost;
            item.sourceTable = "costing_records";
            item.sourceId = costId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = costStmt.getText(1);
            item.intentJson = R"({"estimate_type":"project_estimate"})";
            item.snapshotJson = snapshot.str();
            if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    return created;
}

int ProjectRepository::validateOpenItemsForProject(i64 projectId) {
    int updatedCount = 0;
    auto items = listOpenItemsForProject(projectId);

    for (auto item : items) {
        ProjectOpenItemStatus nextStatus = item.status;
        bool sourceExists = true;
        bool sourceChanged = false;
        auto snapshot = parseSnapshot(item.snapshotJson);

        if (item.sourceTable == "models" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT hash, file_path, file_format,
                       bounds_min_x, bounds_min_y, bounds_min_z,
                       bounds_max_x, bounds_max_y, bounds_max_z
                FROM models
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged = snapshot.value("hash", stmt.getText(0)) != stmt.getText(0) ||
                                snapshot.value("file_path", stmt.getText(1)) != stmt.getText(1) ||
                                snapshot.value("file_format", stmt.getText(2)) != stmt.getText(2);
                if (snapshot.contains("bounds")) {
                    const auto& bounds = snapshot["bounds"];
                    if (bounds.contains("min") && bounds["min"].is_array() &&
                        bounds["min"].size() >= 3) {
                        sourceChanged = sourceChanged ||
                                        std::abs(bounds["min"][0].get<double>() -
                                                 stmt.getDouble(3)) > 0.0001 ||
                                        std::abs(bounds["min"][1].get<double>() -
                                                 stmt.getDouble(4)) > 0.0001 ||
                                        std::abs(bounds["min"][2].get<double>() -
                                                 stmt.getDouble(5)) > 0.0001;
                    }
                    if (bounds.contains("max") && bounds["max"].is_array() &&
                        bounds["max"].size() >= 3) {
                        sourceChanged = sourceChanged ||
                                        std::abs(bounds["max"][0].get<double>() -
                                                 stmt.getDouble(6)) > 0.0001 ||
                                        std::abs(bounds["max"][1].get<double>() -
                                                 stmt.getDouble(7)) > 0.0001 ||
                                        std::abs(bounds["max"][2].get<double>() -
                                                 stmt.getDouble(8)) > 0.0001;
                    }
                }
            }
        } else if (item.sourceTable == "materials" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT name, category, janka_hardness, grain_direction_deg,
                       archive_path, thumbnail_path
                FROM materials
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged = snapshot.value("name", stmt.getText(0)) != stmt.getText(0) ||
                                snapshot.value("category", stmt.getText(1)) != stmt.getText(1) ||
                                doubleChanged(snapshot, "janka_hardness", stmt.getDouble(2)) ||
                                doubleChanged(snapshot, "grain_direction_deg", stmt.getDouble(3)) ||
                                snapshot.value("archive_path", stmt.getText(4)) !=
                                    stmt.getText(4) ||
                                snapshot.value("thumbnail_path", stmt.getText(5)) !=
                                    stmt.getText(5);
            }
        } else if (item.sourceTable == "stock_sizes" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT s.material_id, mat.name, s.name, s.width_mm, s.height_mm,
                       s.thickness_mm, s.price_per_unit, s.unit_label
                FROM stock_sizes s
                INNER JOIN materials mat ON mat.id = s.material_id
                WHERE s.id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged =
                    snapshot.value("material_id", stmt.getInt(0)) != stmt.getInt(0) ||
                    snapshot.value("material_name", stmt.getText(1)) != stmt.getText(1) ||
                    snapshot.value("stock_name", stmt.getText(2)) != stmt.getText(2) ||
                    doubleChanged(snapshot, "width_mm", stmt.getDouble(3)) ||
                    doubleChanged(snapshot, "height_mm", stmt.getDouble(4)) ||
                    doubleChanged(snapshot, "thickness_mm", stmt.getDouble(5)) ||
                    doubleChanged(snapshot, "price_per_unit", stmt.getDouble(6)) ||
                    snapshot.value("unit_label", stmt.getText(7)) != stmt.getText(7);
            }
        } else if (item.sourceTable == "gcode_files" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT hash, file_path, file_size, estimated_time, feed_rates, tool_numbers
                FROM gcode_files
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged = snapshot.value("hash", stmt.getText(0)) != stmt.getText(0) ||
                                snapshot.value("file_path", stmt.getText(1)) != stmt.getText(1) ||
                                snapshot.value("file_size", stmt.getInt(2)) != stmt.getInt(2) ||
                                doubleChanged(snapshot, "estimated_time", stmt.getDouble(3));
                if (snapshot.contains("feed_rates")) {
                    sourceChanged =
                        sourceChanged || snapshot["feed_rates"].dump() != stmt.getText(4);
                }
                if (snapshot.contains("tool_numbers")) {
                    sourceChanged =
                        sourceChanged || snapshot["tool_numbers"].dump() != stmt.getText(5);
                }
            }
        } else if (item.sourceTable == "operation_groups" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare("SELECT model_id, name FROM operation_groups WHERE id = ?");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged =
                    snapshot.value("model_id", stmt.getInt(0)) != stmt.getInt(0) ||
                    snapshot.value("name", stmt.getText(1)) != stmt.getText(1);
            }
        } else if (item.sourceTable == "cut_plans" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT algorithm, allow_rotation, kerf, margin, sheets_used, efficiency
                FROM cut_plans
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged =
                    snapshot.value("algorithm", stmt.getText(0)) != stmt.getText(0) ||
                    snapshot.value("allow_rotation", stmt.getInt(1) != 0) !=
                        (stmt.getInt(1) != 0) ||
                    doubleChanged(snapshot, "kerf", stmt.getDouble(2)) ||
                    doubleChanged(snapshot, "margin", stmt.getDouble(3)) ||
                    snapshot.value("sheets_used", stmt.getInt(4)) != stmt.getInt(4) ||
                    doubleChanged(snapshot, "efficiency", stmt.getDouble(5));
            }
        } else if (item.sourceTable == "costing_records" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT subtotal, tax_amount, discount_amount, total
                FROM costing_records
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged = doubleChanged(snapshot, "subtotal", stmt.getDouble(0)) ||
                                doubleChanged(snapshot, "tax_amount", stmt.getDouble(1)) ||
                                doubleChanged(snapshot, "discount_amount", stmt.getDouble(2)) ||
                                doubleChanged(snapshot, "total", stmt.getDouble(3));
            }
        } else if (item.sourceTable == "cnc_jobs" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT status, error_count, elapsed_seconds
                FROM cnc_jobs
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged =
                    snapshot.value("job_status", stmt.getText(0)) != stmt.getText(0) ||
                    snapshot.value("error_count", stmt.getInt(1)) != stmt.getInt(1) ||
                    doubleChanged(snapshot, "elapsed_seconds", stmt.getDouble(2));
            }
        }

        if (!sourceExists) {
            nextStatus = ProjectOpenItemStatus::Missing;
        } else if (sourceChanged && item.status != ProjectOpenItemStatus::Missing) {
            nextStatus = ProjectOpenItemStatus::Stale;
        }

        if (nextStatus != item.status) {
            item.status = nextStatus;
            if (updateOpenItem(item)) {
                ++updatedCount;
            }
        }
    }

    return updatedCount;
}

i64 ProjectRepository::count() {
    auto stmt = m_db.prepare("SELECT COUNT(*) FROM projects");
    if (!stmt.isValid()) {
        return 0;
    }

    if (stmt.step()) {
        return stmt.getInt(0);
    }

    return 0;
}

ProjectRecord ProjectRepository::rowToProject(Statement& stmt) {
    ProjectRecord project;
    project.id = stmt.getInt(0);
    project.name = stmt.getText(1);
    project.description = stmt.getText(2);
    project.filePath = stmt.getText(3);
    project.notes = stmt.getText(4);
    project.createdAt = stmt.getText(5);
    project.modifiedAt = stmt.getText(6);
    return project;
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
