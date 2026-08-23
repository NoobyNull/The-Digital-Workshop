#include "project_repository.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <nlohmann/json.hpp>

#include "project_repository_open_item_detail.h"

namespace dw {
namespace {

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

bool modelBoundChanged(const nlohmann::json& value, double current) {
    if (!value.is_number())
        return true;
    const double recorded = value.get<double>();
    const double scale = std::max(1.0, std::max(std::abs(recorded), std::abs(current)));
    // Model snapshots written before the precision fix used ostream's default
    // six significant digits. Accept only that narrow rounding envelope; the
    // source hash, path, and format still have to match exactly.
    const double tolerance = std::max(0.0001, scale * 0.000005);
    return std::abs(recorded - current) > tolerance;
}

bool completeModelBounds(const nlohmann::json& snapshot) {
    if (!snapshot.contains("bounds") || !snapshot["bounds"].is_object())
        return false;
    const auto& bounds = snapshot["bounds"];
    for (const char* key : {"min", "max"}) {
        if (!bounds.contains(key) || !bounds[key].is_array() || bounds[key].size() < 3)
            return false;
        for (std::size_t index = 0; index < 3; ++index)
            if (!bounds[key][index].is_number())
                return false;
    }
    return true;
}

bool sourceKeysChanged(const nlohmann::json& snapshot,
                       const std::vector<std::string>& currentKeys) {
    std::vector<std::string> snapshotKeys;
    if (snapshot.contains("source_keys") && snapshot["source_keys"].is_array()) {
        for (const auto& value : snapshot["source_keys"]) {
            if (value.is_string()) {
                snapshotKeys.push_back(value.get<std::string>());
            }
        }
    }
    return snapshotKeys != currentKeys;
}

} // namespace

int ProjectRepository::validateOpenItemsForProject(i64 projectId) {
    int updatedCount = 0;
    auto items = listOpenItemsForProject(projectId);

    for (auto item : items) {
        ProjectOpenItemStatus nextStatus = item.status;
        bool sourceExists = true;
        bool sourceChanged = false;
        bool modelSnapshotCanHeal = false;
        nlohmann::json preciseModelSnapshot;
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
                const auto hash = stmt.getText(0);
                const auto filePath = stmt.getText(1);
                const auto fileFormat = stmt.getText(2);
                const bool completeIdentity =
                    snapshot.contains("hash") && snapshot["hash"].is_string() &&
                    snapshot.contains("file_path") && snapshot["file_path"].is_string() &&
                    snapshot.contains("file_format") && snapshot["file_format"].is_string();
                sourceChanged = snapshot.value("hash", hash) != hash ||
                                snapshot.value("file_path", filePath) != filePath ||
                                snapshot.value("file_format", fileFormat) != fileFormat;
                if (snapshot.contains("bounds")) {
                    const auto& bounds = snapshot["bounds"];
                    if (bounds.contains("min") && bounds["min"].is_array() &&
                        bounds["min"].size() >= 3) {
                        sourceChanged =
                            sourceChanged ||
                            modelBoundChanged(bounds["min"][0], stmt.getDouble(3)) ||
                            modelBoundChanged(bounds["min"][1], stmt.getDouble(4)) ||
                            modelBoundChanged(bounds["min"][2], stmt.getDouble(5));
                    }
                    if (bounds.contains("max") && bounds["max"].is_array() &&
                        bounds["max"].size() >= 3) {
                        sourceChanged =
                            sourceChanged ||
                            modelBoundChanged(bounds["max"][0], stmt.getDouble(6)) ||
                            modelBoundChanged(bounds["max"][1], stmt.getDouble(7)) ||
                            modelBoundChanged(bounds["max"][2], stmt.getDouble(8));
                    }
                }
                modelSnapshotCanHeal = completeIdentity && completeModelBounds(snapshot) &&
                                       !sourceChanged;
                preciseModelSnapshot = nlohmann::json{
                    {"hash", hash},
                    {"file_path", filePath},
                    {"file_format", fileFormat},
                    {"bounds",
                     {{"min", {stmt.getDouble(3), stmt.getDouble(4), stmt.getDouble(5)}},
                      {"max", {stmt.getDouble(6), stmt.getDouble(7), stmt.getDouble(8)}}}},
                };
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
                sourceChanged =
                    snapshot.value("name", stmt.getText(0)) != stmt.getText(0) ||
                    snapshot.value("category", stmt.getText(1)) != stmt.getText(1) ||
                    doubleChanged(snapshot, "janka_hardness", stmt.getDouble(2)) ||
                    doubleChanged(snapshot, "grain_direction_deg", stmt.getDouble(3)) ||
                    snapshot.value("archive_path", stmt.getText(4)) != stmt.getText(4) ||
                    snapshot.value("thumbnail_path", stmt.getText(5)) != stmt.getText(5);
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
                sourceChanged = snapshot.value("material_id", stmt.getInt(0)) != stmt.getInt(0) ||
                                snapshot.value("material_name", stmt.getText(1)) !=
                                    stmt.getText(1) ||
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
                    sourceChanged = sourceChanged ||
                                    !project_open_item_detail::jsonTextSemanticallyEqual(
                                        snapshot["feed_rates"].dump(), stmt.getText(4));
                }
                if (snapshot.contains("tool_numbers")) {
                    sourceChanged = sourceChanged ||
                                    !project_open_item_detail::jsonTextSemanticallyEqual(
                                        snapshot["tool_numbers"].dump(), stmt.getText(5));
                }
            }
        } else if (item.sourceTable == "operation_groups" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare("SELECT model_id, name FROM operation_groups WHERE id = ?");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                sourceChanged = snapshot.value("model_id", stmt.getInt(0)) != stmt.getInt(0) ||
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
                sourceChanged = snapshot.value("algorithm", stmt.getText(0)) != stmt.getText(0) ||
                                snapshot.value("allow_rotation", stmt.getInt(1) != 0) !=
                                    (stmt.getInt(1) != 0) ||
                                doubleChanged(snapshot, "kerf", stmt.getDouble(2)) ||
                                doubleChanged(snapshot, "margin", stmt.getDouble(3)) ||
                                snapshot.value("sheets_used", stmt.getInt(4)) != stmt.getInt(4) ||
                                doubleChanged(snapshot, "efficiency", stmt.getDouble(5));
            }
        } else if (item.sourceTable == "costing_records" && item.sourceId.has_value()) {
            auto stmt = m_db.prepare(R"(
                SELECT subtotal, tax_amount, discount_amount, total, items
                FROM costing_records
                WHERE id = ?
            )");
            if (!stmt.isValid() || !stmt.bindInt(1, *item.sourceId) || !stmt.step()) {
                sourceExists = false;
            } else {
                auto sourceKeys =
                    project_open_item_detail::autoCostSourceKeysFromItemsJson(stmt.getText(4));
                sourceChanged = doubleChanged(snapshot, "subtotal", stmt.getDouble(0)) ||
                                doubleChanged(snapshot, "tax_amount", stmt.getDouble(1)) ||
                                doubleChanged(snapshot, "discount_amount", stmt.getDouble(2)) ||
                                doubleChanged(snapshot, "total", stmt.getDouble(3)) ||
                                sourceKeysChanged(snapshot, sourceKeys);
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
                sourceChanged = snapshot.value("job_status", stmt.getText(0)) != stmt.getText(0) ||
                                snapshot.value("error_count", stmt.getInt(1)) != stmt.getInt(1) ||
                                doubleChanged(snapshot, "elapsed_seconds", stmt.getDouble(2));
            }
        }

        if (!sourceExists) {
            nextStatus = ProjectOpenItemStatus::Missing;
        } else if (sourceChanged && item.status != ProjectOpenItemStatus::Missing) {
            nextStatus = ProjectOpenItemStatus::Stale;
        } else if (item.itemType == ProjectOpenItemType::Model &&
                   item.status == ProjectOpenItemStatus::Stale && modelSnapshotCanHeal) {
            // Stale is a derived source-validation state for model roots. If
            // the exact identity and legacy-rounded bounds now agree, restore
            // the original ready state and upgrade the snapshot in one write.
            nextStatus = ProjectOpenItemStatus::Ready;
            item.snapshotJson = preciseModelSnapshot.dump();
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

} // namespace dw
