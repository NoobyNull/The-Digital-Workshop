#include "project_repository.h"

#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "project_repository_open_item_detail.h"

namespace dw {

int ProjectRepository::ensureOpenItemsForProject(i64 projectId) {
    int created = 0;
    project_open_item_detail::pruneOpenItemsForProject(m_db, projectId);
    const auto explicitPrograms =
        project_open_item_detail::explicitGeneratedDirectCarvePrograms(m_db, projectId);

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

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Model;
            item.sourceTable = "models";
            item.sourceId = modelId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = modelStmt.getText(1);
            item.intentJson = R"({"role":"project_model"})";
            item.snapshotJson = nlohmann::json{
                {"hash", modelStmt.getText(2)},
                {"file_path", modelStmt.getText(3)},
                {"file_format", modelStmt.getText(4)},
                {"bounds",
                 {{"min",
                   {modelStmt.getDouble(5), modelStmt.getDouble(6), modelStmt.getDouble(7)}},
                  {"max",
                   {modelStmt.getDouble(8), modelStmt.getDouble(9), modelStmt.getDouble(10)}}}},
            }.dump();
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
            snapshot << "{\"name\":\""
                     << project_open_item_detail::jsonEscape(materialStmt.getText(1)) << "\","
                     << "\"category\":\""
                     << project_open_item_detail::jsonEscape(materialStmt.getText(2)) << "\","
                     << "\"janka_hardness\":" << materialStmt.getDouble(3) << ","
                     << "\"grain_direction_deg\":" << materialStmt.getDouble(4) << ","
                     << "\"archive_path\":\""
                     << project_open_item_detail::jsonEscape(materialStmt.getText(5)) << "\","
                     << "\"thumbnail_path\":\""
                     << project_open_item_detail::jsonEscape(materialStmt.getText(6)) << "\"}";

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
            snapshot << "{\"material_id\":" << stockStmt.getInt(1) << ",\"material_name\":\""
                     << project_open_item_detail::jsonEscape(materialName) << "\","
                     << "\"stock_name\":\"" << project_open_item_detail::jsonEscape(stockName)
                     << "\","
                     << "\"width_mm\":" << stockStmt.getDouble(3)
                     << ",\"height_mm\":" << stockStmt.getDouble(4)
                     << ",\"thickness_mm\":" << stockStmt.getDouble(5)
                     << ",\"price_per_unit\":" << stockStmt.getDouble(6) << ",\"unit_label\":\""
                     << project_open_item_detail::jsonEscape(stockStmt.getText(7)) << "\"}";

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
            if (!project_open_item_detail::operationGroupNeedsOpenItem(
                    m_db, projectId, groupId, explicitPrograms)) {
                continue;
            }
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
                     << "\"name\":\""
                     << project_open_item_detail::jsonEscape(operationStmt.getText(2)) << "\"}";

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
            const auto parentItemId = project_open_item_detail::operationGroupOpenItemParent(
                m_db, projectId, gcodeId);

            auto existingItems = findOpenItemsBySource(projectId, "gcode_files", gcodeId);
            if (!existingItems.empty()) {
                if (explicitPrograms.find(gcodeId) == explicitPrograms.end() &&
                    parentItemId.has_value() &&
                    existingItems.front().parentItemId != parentItemId) {
                    auto updated = existingItems.front();
                    updated.parentItemId = parentItemId;
                    (void)updateOpenItem(updated);
                }
                continue;
            }

            std::ostringstream snapshot;
            snapshot << "{\"hash\":\"" << project_open_item_detail::jsonEscape(gcodeStmt.getText(2))
                     << "\","
                     << "\"file_path\":\""
                     << project_open_item_detail::jsonEscape(gcodeStmt.getText(3)) << "\","
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
            const auto explicitProgram = explicitPrograms.find(gcodeId);
            if (explicitProgram != explicitPrograms.end()) {
                parentItemId = explicitProgram->second;
            } else {
                parentItemId = project_open_item_detail::operationGroupOpenItemParent(
                    m_db, projectId, gcodeId);
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
                snapshot << "{\"gcode_id\":" << gcodeId << ",\"tool_number\":" << toolNumber << "}";

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
            auto existingItems = findOpenItemsBySource(projectId, "cnc_jobs", jobId);

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
            snapshot << "{\"file_name\":\""
                     << project_open_item_detail::jsonEscape(jobStmt.getText(1)) << "\","
                     << "\"file_path\":\""
                     << project_open_item_detail::jsonEscape(jobStmt.getText(2)) << "\","
                     << "\"total_lines\":" << jobStmt.getInt(3)
                     << ",\"last_acked_line\":" << jobStmt.getInt(4) << ",\"job_status\":\""
                     << project_open_item_detail::jsonEscape(jobStatus) << "\","
                     << "\"error_count\":" << jobStmt.getInt(6)
                     << ",\"elapsed_seconds\":" << jobStmt.getDouble(7) << ",\"started_at\":\""
                     << project_open_item_detail::jsonEscape(jobStmt.getText(8)) << "\","
                     << "\"ended_at\":\""
                     << project_open_item_detail::jsonEscape(jobStmt.getText(9)) << "\"}";

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
            if (!existingItems.empty()) {
                item.id = existingItems.front().id;
                (void)updateOpenItem(item);
            } else if (insertOpenItem(item).has_value()) {
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
            snapshot << "{\"algorithm\":\""
                     << project_open_item_detail::jsonEscape(cutPlanStmt.getText(2)) << "\","
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
        SELECT id, name, subtotal, tax_amount, discount_amount, total, items
        FROM costing_records
        WHERE project_id = ?
        ORDER BY created_at, id
    )");
    if (costStmt.isValid() && costStmt.bindInt(1, projectId)) {
        while (costStmt.step()) {
            i64 costId = costStmt.getInt(0);
            auto existingItems = findOpenItemsBySource(projectId, "costing_records", costId);
            auto sourceKeys =
                project_open_item_detail::autoCostSourceKeysFromItemsJson(costStmt.getText(6));

            std::ostringstream snapshot;
            snapshot << "{\"subtotal\":" << costStmt.getDouble(2)
                     << ",\"tax_amount\":" << costStmt.getDouble(3)
                     << ",\"discount_amount\":" << costStmt.getDouble(4)
                     << ",\"total\":" << costStmt.getDouble(5)
                     << ",\"source_keys\":" << project_open_item_detail::jsonStringArray(sourceKeys)
                     << "}";

            ProjectOpenItem item;
            item.projectId = projectId;
            item.itemType = ProjectOpenItemType::Cost;
            item.sourceTable = "costing_records";
            item.sourceId = costId;
            item.status = ProjectOpenItemStatus::Ready;
            item.displayName = costStmt.getText(1);
            item.intentJson = R"({"estimate_type":"project_estimate"})";
            item.snapshotJson = snapshot.str();
            if (!existingItems.empty()) {
                item.id = existingItems.front().id;
                (void)updateOpenItem(item);
            } else if (insertOpenItem(item).has_value()) {
                ++created;
            }
        }
    }

    return created;
}

} // namespace dw
