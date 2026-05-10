#include "model_repository.h"

#include <sstream>

#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace dw {

ModelRepository::ModelRepository(Database& db) : m_db(db) {}

std::optional<i64> ModelRepository::insert(const ModelRecord& model) {
    auto stmt = m_db.prepare(R"(
        INSERT INTO models (
            hash, name, file_path, file_format, file_size,
            vertex_count, triangle_count,
            bounds_min_x, bounds_min_y, bounds_min_z,
            bounds_max_x, bounds_max_y, bounds_max_z,
            thumbnail_path, tags, orient_yaw, orient_matrix
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    if (!stmt.isValid()) {
        return std::nullopt;
    }

    if (!stmt.bindText(1, model.hash) || !stmt.bindText(2, model.name) ||
        !stmt.bindText(3, model.filePath.string()) || !stmt.bindText(4, model.fileFormat) ||
        !stmt.bindInt(5, static_cast<i64>(model.fileSize)) ||
        !stmt.bindInt(6, static_cast<i64>(model.vertexCount)) ||
        !stmt.bindInt(7, static_cast<i64>(model.triangleCount)) ||
        !stmt.bindDouble(8, static_cast<f64>(model.boundsMin.x)) ||
        !stmt.bindDouble(9, static_cast<f64>(model.boundsMin.y)) ||
        !stmt.bindDouble(10, static_cast<f64>(model.boundsMin.z)) ||
        !stmt.bindDouble(11, static_cast<f64>(model.boundsMax.x)) ||
        !stmt.bindDouble(12, static_cast<f64>(model.boundsMax.y)) ||
        !stmt.bindDouble(13, static_cast<f64>(model.boundsMax.z)) ||
        !stmt.bindText(14, model.thumbnailPath.string()) ||
        !stmt.bindText(15, tagsToJson(model.tags))) {
        log::error("ModelRepo", "Failed to bind insert parameters");
        return std::nullopt;
    }

    // Bind orient columns (NULL if not computed)
    if (model.orientYaw) {
        if (!stmt.bindDouble(16, static_cast<f64>(*model.orientYaw))) {
            return std::nullopt;
        }
    } else {
        if (!stmt.bindNull(16)) {
            return std::nullopt;
        }
    }
    if (model.orientMatrix) {
        if (!stmt.bindText(17, mat4ToJson(*model.orientMatrix))) {
            return std::nullopt;
        }
    } else {
        if (!stmt.bindNull(17)) {
            return std::nullopt;
        }
    }

    if (!stmt.execute()) {
        log::errorf("ModelRepo", "Failed to insert model: %s", m_db.lastError().c_str());
        return std::nullopt;
    }

    return m_db.lastInsertId();
}

std::optional<ModelRecord> ModelRepository::findById(i64 id) {
    auto stmt = m_db.prepare("SELECT * FROM models WHERE id = ?");
    if (!stmt.isValid()) {
        return std::nullopt;
    }

    if (!stmt.bindInt(1, id)) {
        return std::nullopt;
    }

    if (stmt.step()) {
        return rowToModel(stmt);
    }

    return std::nullopt;
}

std::optional<ModelRecord> ModelRepository::findByHash(std::string_view hash) {
    auto stmt = m_db.prepare("SELECT * FROM models WHERE hash = ?");
    if (!stmt.isValid()) {
        return std::nullopt;
    }

    if (!stmt.bindText(1, std::string(hash))) {
        return std::nullopt;
    }

    if (stmt.step()) {
        return rowToModel(stmt);
    }

    return std::nullopt;
}

std::vector<ModelRecord> ModelRepository::findAll() {
    std::vector<ModelRecord> results;

    auto stmt = m_db.prepare("SELECT * FROM models ORDER BY imported_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }

    return results;
}

std::vector<ModelRecord> ModelRepository::findByName(std::string_view searchTerm) {
    std::vector<ModelRecord> results;

    auto stmt = m_db.prepare(
        "SELECT * FROM models WHERE name LIKE ? ESCAPE '\\' ORDER BY imported_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    if (!stmt.bindText(1, "%" + str::escapeLike(searchTerm) + "%")) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }

    return results;
}

std::vector<ModelRecord> ModelRepository::findByFormat(std::string_view format) {
    std::vector<ModelRecord> results;

    auto stmt =
        m_db.prepare("SELECT * FROM models WHERE file_format = ? ORDER BY imported_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    if (!stmt.bindText(1, std::string(format))) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }

    return results;
}

std::vector<ModelRecord> ModelRepository::findByTag(std::string_view tag) {
    std::vector<ModelRecord> results;

    // Simple JSON search using LIKE (for now)
    auto stmt = m_db.prepare(
        "SELECT * FROM models WHERE tags LIKE ? ESCAPE '\\' ORDER BY imported_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    if (!stmt.bindText(1, "%\"" + str::escapeLike(tag) + "\"%")) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }

    return results;
}

std::vector<ModelRecord> ModelRepository::findByTagStatus(int status) {
    std::vector<ModelRecord> results;

    auto stmt =
        m_db.prepare("SELECT * FROM models WHERE tag_status = ? ORDER BY imported_at DESC");
    if (!stmt.isValid()) {
        return results;
    }

    if (!stmt.bindInt(1, static_cast<i64>(status))) {
        return results;
    }

    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }

    return results;
}

bool ModelRepository::update(const ModelRecord& model) {
    auto stmt = m_db.prepare(R"(
        UPDATE models SET
            name = ?,
            file_path = ?,
            file_format = ?,
            file_size = ?,
            vertex_count = ?,
            triangle_count = ?,
            bounds_min_x = ?,
            bounds_min_y = ?,
            bounds_min_z = ?,
            bounds_max_x = ?,
            bounds_max_y = ?,
            bounds_max_z = ?,
            thumbnail_path = ?,
            tags = ?
        WHERE id = ?
    )");

    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, model.name) || !stmt.bindText(2, model.filePath.string()) ||
        !stmt.bindText(3, model.fileFormat) || !stmt.bindInt(4, static_cast<i64>(model.fileSize)) ||
        !stmt.bindInt(5, static_cast<i64>(model.vertexCount)) ||
        !stmt.bindInt(6, static_cast<i64>(model.triangleCount)) ||
        !stmt.bindDouble(7, static_cast<f64>(model.boundsMin.x)) ||
        !stmt.bindDouble(8, static_cast<f64>(model.boundsMin.y)) ||
        !stmt.bindDouble(9, static_cast<f64>(model.boundsMin.z)) ||
        !stmt.bindDouble(10, static_cast<f64>(model.boundsMax.x)) ||
        !stmt.bindDouble(11, static_cast<f64>(model.boundsMax.y)) ||
        !stmt.bindDouble(12, static_cast<f64>(model.boundsMax.z)) ||
        !stmt.bindText(13, model.thumbnailPath.string()) ||
        !stmt.bindText(14, tagsToJson(model.tags)) || !stmt.bindInt(15, model.id)) {
        return false;
    }

    return stmt.execute();
}

bool ModelRepository::updateThumbnail(i64 id, const Path& thumbnailPath) {
    auto stmt = m_db.prepare("UPDATE models SET thumbnail_path = ? WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, thumbnailPath.string()) || !stmt.bindInt(2, id)) {
        return false;
    }

    return stmt.execute();
}

bool ModelRepository::updateTags(i64 id, const std::vector<std::string>& tags) {
    auto stmt = m_db.prepare("UPDATE models SET tags = ? WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, tagsToJson(tags)) || !stmt.bindInt(2, id)) {
        return false;
    }

    return stmt.execute();
}

bool ModelRepository::remove(i64 id) {
    auto stmt = m_db.prepare("DELETE FROM models WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindInt(1, id)) {
        return false;
    }
    return stmt.execute();
}

bool ModelRepository::removeByHash(std::string_view hash) {
    auto stmt = m_db.prepare("DELETE FROM models WHERE hash = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, std::string(hash))) {
        return false;
    }
    return stmt.execute();
}

bool ModelRepository::exists(std::string_view hash) {
    auto stmt = m_db.prepare("SELECT 1 FROM models WHERE hash = ? LIMIT 1");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, std::string(hash))) {
        return false;
    }
    return stmt.step();
}

i64 ModelRepository::count() {
    auto stmt = m_db.prepare("SELECT COUNT(*) FROM models");
    if (!stmt.isValid()) {
        return 0;
    }

    if (stmt.step()) {
        return stmt.getInt(0);
    }

    return 0;
}

std::vector<ModelRecord> ModelRepository::searchFTS(const std::string& query) {
    std::vector<ModelRecord> results;
    if (query.empty())
        return results;

    // Add prefix wildcard for search-as-you-type if not already present
    std::string ftsQuery = query;
    if (!ftsQuery.empty() && ftsQuery.back() != '*') {
        ftsQuery += '*';
    }

    auto stmt = m_db.prepare("SELECT m.* FROM models m "
                             "INNER JOIN models_fts ON models_fts.rowid = m.id "
                             "WHERE models_fts MATCH ? "
                             "ORDER BY bm25(models_fts, 10.0, 3.0) "
                             "LIMIT 500");
    if (!stmt.isValid())
        return results;
    if (!stmt.bindText(1, ftsQuery))
        return results;
    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }
    return results;
}

bool ModelRepository::assignCategory(i64 modelId, i64 categoryId) {
    auto stmt = m_db.prepare(
        "INSERT OR IGNORE INTO model_categories (model_id, category_id) VALUES (?, ?)");
    if (!stmt.isValid())
        return false;
    if (!stmt.bindInt(1, modelId) || !stmt.bindInt(2, categoryId))
        return false;
    return stmt.execute();
}

bool ModelRepository::removeCategory(i64 modelId, i64 categoryId) {
    auto stmt = m_db.prepare("DELETE FROM model_categories WHERE model_id = ? AND category_id = ?");
    if (!stmt.isValid())
        return false;
    if (!stmt.bindInt(1, modelId) || !stmt.bindInt(2, categoryId))
        return false;
    return stmt.execute();
}

std::vector<ModelRecord> ModelRepository::findByCategory(i64 categoryId) {
    std::vector<ModelRecord> results;
    auto stmt = m_db.prepare("WITH RECURSIVE subtree(id) AS ("
                             "  SELECT ? "
                             "  UNION ALL "
                             "  SELECT c.id FROM categories c "
                             "  INNER JOIN subtree s ON c.parent_id = s.id"
                             ") "
                             "SELECT DISTINCT m.* FROM models m "
                             "INNER JOIN model_categories mc ON mc.model_id = m.id "
                             "INNER JOIN subtree st ON mc.category_id = st.id "
                             "ORDER BY m.imported_at DESC");
    if (!stmt.isValid())
        return results;
    if (!stmt.bindInt(1, categoryId))
        return results;
    while (stmt.step()) {
        results.push_back(rowToModel(stmt));
    }
    return results;
}

std::optional<i64> ModelRepository::createCategory(const std::string& name,
                                                   std::optional<i64> parentId) {
    auto stmt = m_db.prepare("INSERT INTO categories (name, parent_id) VALUES (?, ?)");
    if (!stmt.isValid())
        return std::nullopt;
    if (!stmt.bindText(1, name))
        return std::nullopt;
    if (parentId) {
        if (!stmt.bindInt(2, *parentId))
            return std::nullopt;
    } else {
        if (!stmt.bindNull(2))
            return std::nullopt;
    }
    if (!stmt.execute())
        return std::nullopt;
    return m_db.lastInsertId();
}

bool ModelRepository::deleteCategory(i64 categoryId) {
    auto stmt = m_db.prepare("DELETE FROM categories WHERE id = ?");
    if (!stmt.isValid())
        return false;
    if (!stmt.bindInt(1, categoryId))
        return false;
    return stmt.execute();
}

std::vector<CategoryRecord> ModelRepository::getAllCategories() {
    std::vector<CategoryRecord> results;
    auto stmt = m_db.prepare("SELECT id, name, parent_id, sort_order FROM categories "
                             "ORDER BY parent_id, sort_order, name");
    if (!stmt.isValid())
        return results;
    while (stmt.step()) {
        CategoryRecord cat;
        cat.id = stmt.getInt(0);
        cat.name = stmt.getText(1);
        if (!stmt.isNull(2)) {
            cat.parentId = stmt.getInt(2);
        }
        cat.sortOrder = static_cast<int>(stmt.getInt(3));
        results.push_back(std::move(cat));
    }
    return results;
}

std::vector<CategoryRecord> ModelRepository::getChildCategories(i64 parentId) {
    std::vector<CategoryRecord> results;
    auto stmt = m_db.prepare("SELECT id, name, parent_id, sort_order FROM categories "
                             "WHERE parent_id = ? ORDER BY sort_order, name");
    if (!stmt.isValid())
        return results;
    if (!stmt.bindInt(1, parentId))
        return results;
    while (stmt.step()) {
        CategoryRecord cat;
        cat.id = stmt.getInt(0);
        cat.name = stmt.getText(1);
        cat.parentId = parentId;
        cat.sortOrder = static_cast<int>(stmt.getInt(3));
        results.push_back(std::move(cat));
    }
    return results;
}

std::vector<CategoryRecord> ModelRepository::getRootCategories() {
    std::vector<CategoryRecord> results;
    auto stmt = m_db.prepare("SELECT id, name, parent_id, sort_order FROM categories "
                             "WHERE parent_id IS NULL ORDER BY sort_order, name");
    if (!stmt.isValid())
        return results;
    while (stmt.step()) {
        CategoryRecord cat;
        cat.id = stmt.getInt(0);
        cat.name = stmt.getText(1);
        cat.sortOrder = static_cast<int>(stmt.getInt(3));
        results.push_back(std::move(cat));
    }
    return results;
}

std::optional<i64> ModelRepository::findCategoryByNameAndParent(const std::string& name,
                                                                std::optional<i64> parentId) {
    std::string query;
    auto stmt = m_db.prepare("SELECT id FROM categories WHERE name = ? AND parent_id IS ?");
    if (!stmt.isValid())
        return std::nullopt;

    if (!stmt.bindText(1, name))
        return std::nullopt;

    if (parentId) {
        // Use a different prepared statement for non-NULL parent_id
        stmt = m_db.prepare("SELECT id FROM categories WHERE name = ? AND parent_id = ?");
        if (!stmt.isValid())
            return std::nullopt;
        if (!stmt.bindText(1, name) || !stmt.bindInt(2, *parentId))
            return std::nullopt;
    } else {
        if (!stmt.bindNull(2))
            return std::nullopt;
    }

    if (stmt.step()) {
        return stmt.getInt(0);
    }
    return std::nullopt;
}

ModelRecord ModelRepository::rowToModel(Statement& stmt) {
    ModelRecord model;
    model.id = stmt.getInt(0);
    model.hash = stmt.getText(1);
    model.name = stmt.getText(2);
    model.filePath = stmt.getText(3);
    model.fileFormat = stmt.getText(4);
    model.fileSize = static_cast<u64>(stmt.getInt(5));
    model.vertexCount = static_cast<u32>(stmt.getInt(6));
    model.triangleCount = static_cast<u32>(stmt.getInt(7));
    model.boundsMin.x = static_cast<f32>(stmt.getDouble(8));
    model.boundsMin.y = static_cast<f32>(stmt.getDouble(9));
    model.boundsMin.z = static_cast<f32>(stmt.getDouble(10));
    model.boundsMax.x = static_cast<f32>(stmt.getDouble(11));
    model.boundsMax.y = static_cast<f32>(stmt.getDouble(12));
    model.boundsMax.z = static_cast<f32>(stmt.getDouble(13));
    model.thumbnailPath = stmt.getText(14);
    model.importedAt = stmt.getText(15);
    model.tags = jsonToTags(stmt.getText(16));
    // Column 17 = material_id (read elsewhere)
    // Column 18 = orient_yaw, Column 19 = orient_matrix
    if (!stmt.isNull(18)) {
        model.orientYaw = static_cast<f32>(stmt.getDouble(18));
    }
    if (!stmt.isNull(19)) {
        model.orientMatrix = jsonToMat4(stmt.getText(19));
    }
    // Columns 20-25: camera state (distance, pitch, yaw, target xyz)
    if (!stmt.isNull(20)) {
        CameraState cam;
        cam.distance = static_cast<f32>(stmt.getDouble(20));
        cam.pitch = static_cast<f32>(stmt.getDouble(21));
        cam.yaw = static_cast<f32>(stmt.getDouble(22));
        cam.target.x = static_cast<f32>(stmt.getDouble(23));
        cam.target.y = static_cast<f32>(stmt.getDouble(24));
        cam.target.z = static_cast<f32>(stmt.getDouble(25));
        model.cameraState = cam;
    }
    // Columns 26-28: descriptor fields (title, description, hover)
    model.descriptorTitle = stmt.getText(26);
    model.descriptorDescription = stmt.getText(27);
    model.descriptorHover = stmt.getText(28);
    // Column 29: tag_status
    model.tagStatus = static_cast<int>(stmt.getInt(29));
    return model;
}

bool ModelRepository::updateOrient(i64 id, f32 yaw, const Mat4& matrix) {
    auto stmt = m_db.prepare("UPDATE models SET orient_yaw = ?, orient_matrix = ? WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindDouble(1, static_cast<f64>(yaw)) || !stmt.bindText(2, mat4ToJson(matrix)) ||
        !stmt.bindInt(3, id)) {
        return false;
    }

    return stmt.execute();
}

bool ModelRepository::updateCameraState(i64 id, const CameraState& state) {
    auto stmt = m_db.prepare(R"(
        UPDATE models SET
            camera_distance = ?,
            camera_pitch = ?,
            camera_yaw = ?,
            camera_target_x = ?,
            camera_target_y = ?,
            camera_target_z = ?
        WHERE id = ?
    )");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindDouble(1, static_cast<f64>(state.distance)) ||
        !stmt.bindDouble(2, static_cast<f64>(state.pitch)) ||
        !stmt.bindDouble(3, static_cast<f64>(state.yaw)) ||
        !stmt.bindDouble(4, static_cast<f64>(state.target.x)) ||
        !stmt.bindDouble(5, static_cast<f64>(state.target.y)) ||
        !stmt.bindDouble(6, static_cast<f64>(state.target.z)) || !stmt.bindInt(7, id)) {
        return false;
    }

    return stmt.execute();
}

bool ModelRepository::updateDescriptor(i64 id,
                                       const std::string& title,
                                       const std::string& description,
                                       const std::string& hover) {
    auto stmt = m_db.prepare(R"(
        UPDATE models SET
            descriptor_title = ?,
            descriptor_description = ?,
            descriptor_hover = ?
        WHERE id = ?
    )");
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, title) || !stmt.bindText(2, description) || !stmt.bindText(3, hover) ||
        !stmt.bindInt(4, id)) {
        return false;
    }

    return stmt.execute();
}

bool ModelRepository::clearAiClassification(i64 id) {
    auto stmt = m_db.prepare(R"(
        UPDATE models SET
            descriptor_title = '',
            descriptor_description = '',
            descriptor_hover = '',
            tags = '[]',
            tag_status = 0
        WHERE id = ?
    )");
    if (!stmt.isValid())
        return false;
    if (!stmt.bindInt(1, id))
        return false;
    if (!stmt.execute())
        return false;

    auto categories = m_db.prepare("DELETE FROM model_categories WHERE model_id = ?");
    if (!categories.isValid())
        return false;
    if (!categories.bindInt(1, id))
        return false;
    return categories.execute();
}

bool ModelRepository::updateTagStatus(i64 id, int status) {
    auto stmt = m_db.prepare("UPDATE models SET tag_status = ? WHERE id = ?");
    if (!stmt.isValid())
        return false;
    if (!stmt.bindInt(1, static_cast<i64>(status)) || !stmt.bindInt(2, id))
        return false;
    return stmt.execute();
}

int ModelRepository::resetTagStatus(int fromStatus, int toStatus) {
    auto stmt = m_db.prepare("UPDATE models SET tag_status = ? WHERE tag_status = ?");
    if (!stmt.isValid())
        return 0;
    if (!stmt.bindInt(1, static_cast<i64>(toStatus)) ||
        !stmt.bindInt(2, static_cast<i64>(fromStatus)))
        return 0;
    if (!stmt.execute())
        return 0;
    return m_db.changesCount();
}

int ModelRepository::recoverInterruptedTagStatuses() {
    int changed = 0;

    if (m_db.execute(R"(
        UPDATE models
        SET tag_status = 2
        WHERE tag_status = 1
          AND COALESCE(descriptor_title, '') != ''
          AND COALESCE(descriptor_description, '') != ''
    )")) {
        changed += m_db.changesCount();
    }

    if (m_db.execute(R"(
        UPDATE models
        SET tag_status = 0
        WHERE tag_status = 1
          AND (
              COALESCE(descriptor_title, '') = ''
              OR COALESCE(descriptor_description, '') = ''
          )
    )")) {
        changed += m_db.changesCount();
    }

    if (m_db.execute(R"(
        UPDATE models
        SET tag_status = 0
        WHERE tag_status = 2
          AND (
              COALESCE(descriptor_title, '') = ''
              OR COALESCE(descriptor_description, '') = ''
          )
    )")) {
        changed += m_db.changesCount();
    }

    return changed;
}

std::optional<ModelRecord> ModelRepository::findNextUntagged() {
    auto stmt = m_db.prepare(
        "SELECT * FROM models WHERE tag_status = 0 AND thumbnail_path IS NOT NULL "
        "AND thumbnail_path != '' LIMIT 1");
    if (!stmt.isValid())
        return std::nullopt;
    if (stmt.step())
        return rowToModel(stmt);
    return std::nullopt;
}

std::optional<ModelRecord> ModelRepository::findNextAiTagCandidate(i64 afterId) {
    auto stmt = m_db.prepare(
        "SELECT * FROM models "
        "WHERE tag_status IN (0, 3, 4) "
        "AND id > ? "
        "AND thumbnail_path IS NOT NULL "
        "AND thumbnail_path != '' "
        "ORDER BY id ASC "
        "LIMIT 1");
    if (!stmt.isValid())
        return std::nullopt;
    if (!stmt.bindInt(1, afterId))
        return std::nullopt;
    if (stmt.step())
        return rowToModel(stmt);
    return std::nullopt;
}

int ModelRepository::countByTagStatus(int status) {
    auto stmt = m_db.prepare("SELECT COUNT(*) FROM models WHERE tag_status = ?");
    if (!stmt.isValid())
        return 0;
    if (!stmt.bindInt(1, static_cast<i64>(status)))
        return 0;
    if (stmt.step())
        return static_cast<int>(stmt.getInt(0));
    return 0;
}

int ModelRepository::countAiTagCandidates() {
    auto stmt = m_db.prepare(
        "SELECT COUNT(*) FROM models "
        "WHERE tag_status IN (0, 3, 4) "
        "AND thumbnail_path IS NOT NULL "
        "AND thumbnail_path != ''");
    if (!stmt.isValid())
        return 0;
    if (stmt.step())
        return static_cast<int>(stmt.getInt(0));
    return 0;
}

std::string ModelRepository::mat4ToJson(const Mat4& m) {
    std::ostringstream ss;
    ss << "[";
    const f32* data = glm::value_ptr(m);
    for (int i = 0; i < 16; ++i) {
        if (i > 0) {
            ss << ",";
        }
        ss << data[i];
    }
    ss << "]";
    return ss.str();
}

std::optional<Mat4> ModelRepository::jsonToMat4(const std::string& json) {
    if (json.size() < 2 || json[0] != '[') {
        return std::nullopt;
    }

    Mat4 result(1.0f);
    f32* data = glm::value_ptr(result);
    int index = 0;

    std::istringstream ss(json.substr(1, json.size() - 2));
    std::string token;
    while (std::getline(ss, token, ',') && index < 16) {
        data[index++] = std::stof(token);
    }

    if (index != 16) {
        return std::nullopt;
    }
    return result;
}

std::string ModelRepository::tagsToJson(const std::vector<std::string>& tags) {
    if (tags.empty()) {
        return "[]";
    }

    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) {
            ss << ",";
        }
        ss << "\"" << str::escapeJsonString(tags[i]) << "\"";
    }
    ss << "]";
    return ss.str();
}

std::vector<std::string> ModelRepository::jsonToTags(const std::string& json) {
    std::vector<std::string> tags;

    if (json.size() < 2 || json[0] != '[') {
        return tags;
    }

    size_t pos = 1;
    while (pos < json.size()) {
        // Find opening quote
        size_t start = json.find('"', pos);
        if (start == std::string::npos) {
            break;
        }

        // Find closing quote, skipping escaped quotes
        size_t end = start + 1;
        while (end < json.size()) {
            if (json[end] == '"' && json[end - 1] != '\\') {
                break;
            }
            ++end;
        }
        if (end >= json.size()) {
            break;
        }

        // Unescape the string
        std::string tag;
        for (size_t i = start + 1; i < end; ++i) {
            if (json[i] == '\\' && i + 1 < end) {
                char next = json[i + 1];
                if (next == '"' || next == '\\') {
                    tag.push_back(next);
                    ++i;
                    continue;
                }
            }
            tag.push_back(json[i]);
        }
        tags.push_back(std::move(tag));
        pos = end + 1;
    }

    return tags;
}

} // namespace dw
