#include "project_repository_open_item_detail.h"

#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "database.h"

namespace dw::project_open_item_detail {

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}
std::vector<std::string> autoCostSourceKeysFromItemsJson(const std::string& itemsJson) {
    std::set<std::string> keys;
    auto parsed = nlohmann::json::parse(itemsJson, nullptr, false);
    if (!parsed.is_array()) {
        return {};
    }

    for (const auto& item : parsed) {
        if (!item.is_object() || !item.contains("notes") || !item["notes"].is_string()) {
            continue;
        }
        auto notes = item["notes"].get<std::string>();
        if (notes.rfind("[auto:", 0) == 0) {
            keys.insert(std::move(notes));
        }
    }

    return {keys.begin(), keys.end()};
}

std::string jsonStringArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << jsonEscape(values[i]) << "\"";
    }
    out << "]";
    return out.str();
}

GeneratedDirectCarvePrograms explicitGeneratedDirectCarvePrograms(Database& database,
                                                                  i64 projectId) {
    GeneratedDirectCarvePrograms programs;
    auto stmt = database.prepare(R"(
        SELECT child.source_id, child.id, child.intent_json
        FROM project_open_items child
        INNER JOIN project_open_items parent ON parent.id = child.parent_item_id
        WHERE child.project_id = ?
          AND child.item_type = 'gcode'
          AND child.source_table = 'gcode_files'
          AND child.source_id IS NOT NULL
          AND parent.project_id = child.project_id
          AND parent.item_type = 'operation'
          AND parent.source_table = 'direct_carve'
        ORDER BY child.id
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, projectId)) {
        return programs;
    }
    while (stmt.step()) {
        const auto intent = nlohmann::json::parse(stmt.getText(2), nullptr, false);
        if (intent.is_object() &&
            intent.value("role", std::string{}) == "generated_direct_carve_program") {
            programs.emplace(stmt.getInt(0), stmt.getInt(1));
        }
    }
    return programs;
}

bool operationGroupNeedsOpenItem(
    Database& database,
    i64 projectId,
    i64 groupId,
    const GeneratedDirectCarvePrograms& explicitPrograms) {
    auto stmt = database.prepare(R"(
        SELECT gm.gcode_id
        FROM gcode_group_members gm
        INNER JOIN project_gcode pg ON pg.gcode_id = gm.gcode_id
        WHERE gm.group_id = ? AND pg.project_id = ?
        ORDER BY gm.gcode_id
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, groupId) || !stmt.bindInt(2, projectId)) {
        return true;
    }

    bool foundMember = false;
    while (stmt.step()) {
        foundMember = true;
        if (explicitPrograms.find(stmt.getInt(0)) == explicitPrograms.end()) {
            return true;
        }
    }
    return !foundMember;
}

std::optional<i64> operationGroupOpenItemParent(Database& database,
                                                i64 projectId,
                                                i64 gcodeId) {
    auto stmt = database.prepare(R"(
        SELECT item.id
        FROM operation_groups og
        INNER JOIN gcode_group_members gm ON gm.group_id = og.id
        INNER JOIN project_open_items item
                ON item.project_id = ?
               AND item.source_table = 'operation_groups'
               AND item.source_id = og.id
        WHERE gm.gcode_id = ?
        ORDER BY og.id, item.id
        LIMIT 1
    )");
    if (!stmt.isValid() || !stmt.bindInt(1, projectId) || !stmt.bindInt(2, gcodeId) ||
        !stmt.step()) {
        return std::nullopt;
    }
    return stmt.getInt(0);
}

bool jsonTextSemanticallyEqual(const std::string& left, const std::string& right) {
    const auto parsedLeft = nlohmann::json::parse(left, nullptr, false);
    const auto parsedRight = nlohmann::json::parse(right, nullptr, false);
    return !parsedLeft.is_discarded() && !parsedRight.is_discarded() &&
           parsedLeft == parsedRight;
}

void pruneOpenItemsForProject(Database& database, i64 projectId) {
    auto pruneModels = database.prepare(R"(
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

    auto pruneGcode = database.prepare(R"(
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

    auto pruneJobs = database.prepare(R"(
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
    if (pruneJobs.isValid() && pruneJobs.bindInt(1, projectId) && pruneJobs.bindInt(2, projectId)) {
        (void)pruneJobs.execute();
    }

    auto pruneCutPlans = database.prepare(R"(
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

    auto pruneCosts = database.prepare(R"(
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

    auto pruneStock = database.prepare(R"(
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
}

} // namespace dw::project_open_item_detail
