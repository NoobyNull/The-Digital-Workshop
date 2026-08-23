#include "supplier_tool_catalog.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "database.h"

namespace dw {
namespace {

template <typename T>
using RowMap = std::unordered_map<std::string, T>;

struct SourceRows {
    RowMap<VtdbMaterial> materials;
    RowMap<VtdbMachine> machines;
    RowMap<VtdbToolGeometry> geometries;
    RowMap<VtdbCuttingData> cuttingData;
    std::vector<std::string> geometryOrder;
    std::vector<VtdbToolEntity> entities;
    std::vector<VtdbTreeEntry> treeEntries;
};

SupplierToolCatalogOpenResult failure(SupplierToolCatalogError error,
                                      std::string message) {
    return {error, std::move(message)};
}

bool hasTable(Database& db, const std::string& table) {
    auto query = db.prepare(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
    return query.isValid() && query.bindText(1, table) && query.step();
}

std::set<std::string> columnsFor(Database& db, const std::string& table) {
    std::set<std::string> columns;
    auto query = db.prepare("PRAGMA table_info(\"" + table + "\")");
    while (query.step())
        columns.insert(query.getText(1));
    return columns;
}

bool hasColumns(const std::set<std::string>& actual,
                const std::vector<std::string>& required) {
    return std::all_of(required.begin(), required.end(), [&](const auto& column) {
        return actual.count(column) != 0;
    });
}

bool readMaterials(Database& db, SourceRows& rows) {
    auto query = db.prepare("SELECT id, name FROM material");
    if (!query.isValid()) return false;
    while (query.step()) {
        VtdbMaterial value;
        value.id = query.getText(0);
        value.name = query.getText(1);
        rows.materials.emplace(value.id, std::move(value));
    }
    return true;
}

bool readMachines(Database& db, SourceRows& rows) {
    const auto columns = columnsFor(db, "machine");
    const auto optional = [&](const char* name, const char* fallback) {
        return columns.count(name) ? std::string(name) : std::string(fallback);
    };
    const std::string sql =
        "SELECT id, name, make, model, controller_type, dimensions_units, "
        "max_width, max_height, support_rotary, support_tool_change, has_laser_head, "
        + optional("spindle_power_watts", "0") + ", "
        + optional("max_rpm", "24000") + ", "
        + optional("drive_type", "0") + " FROM machine";
    auto query = db.prepare(sql);
    if (!query.isValid()) return false;
    while (query.step()) {
        VtdbMachine value;
        value.id = query.getText(0);
        value.name = query.getText(1);
        value.make = query.getText(2);
        value.model = query.getText(3);
        value.controller_type = query.getText(4);
        value.dimensions_units = static_cast<int>(query.getInt(5));
        value.max_width = query.getDouble(6);
        value.max_height = query.getDouble(7);
        value.support_rotary = static_cast<int>(query.getInt(8));
        value.support_tool_change = static_cast<int>(query.getInt(9));
        value.has_laser_head = static_cast<int>(query.getInt(10));
        value.spindle_power_watts = query.getDouble(11);
        value.max_rpm = static_cast<int>(query.getInt(12));
        value.drive_type = static_cast<DriveType>(query.getInt(13));
        rows.machines.emplace(value.id, std::move(value));
    }
    return true;
}

bool readGeometries(Database& db, SourceRows& rows) {
    auto query = db.prepare(
        "SELECT id, name_format, notes, tool_type, units, diameter, included_angle, "
        "flat_diameter, num_flutes, flute_length, thread_pitch, outline, tip_radius, "
        "laser_watt, custom_attributes, tooth_size, tooth_offset, neck_length, "
        "tooth_height, threaded_length FROM tool_geometry ORDER BY rowid");
    if (!query.isValid()) return false;
    while (query.step()) {
        VtdbToolGeometry value;
        value.id = query.getText(0);
        value.name_format = query.getText(1);
        value.notes = query.getText(2);
        value.tool_type = static_cast<VtdbToolType>(query.getInt(3));
        value.units = static_cast<VtdbUnits>(query.getInt(4));
        value.diameter = query.getDouble(5);
        value.included_angle = query.getDouble(6);
        value.flat_diameter = query.getDouble(7);
        value.num_flutes = static_cast<int>(query.getInt(8));
        value.flute_length = query.getDouble(9);
        value.thread_pitch = query.getDouble(10);
        value.outline = query.getBlob(11);
        value.tip_radius = query.getDouble(12);
        value.laser_watt = static_cast<int>(query.getInt(13));
        value.custom_attributes = query.getText(14);
        value.tooth_size = query.getDouble(15);
        value.tooth_offset = query.getDouble(16);
        value.neck_length = query.getDouble(17);
        value.tooth_height = query.getDouble(18);
        value.threaded_length = query.getDouble(19);
        rows.geometryOrder.push_back(value.id);
        rows.geometries.emplace(value.id, std::move(value));
    }
    return true;
}

bool readCuttingData(Database& db, SourceRows& rows) {
    auto query = db.prepare(
        "SELECT id, rate_units, feed_rate, plunge_rate, spindle_speed, spindle_dir, "
        "stepdown, stepover, clear_stepover, thread_depth, thread_step_in, "
        "laser_power, laser_passes, laser_burn_rate, line_width, length_units, "
        "tool_number, laser_kerf, notes FROM tool_cutting_data");
    if (!query.isValid()) return false;
    while (query.step()) {
        VtdbCuttingData value;
        value.id = query.getText(0);
        value.rate_units = static_cast<int>(query.getInt(1));
        value.feed_rate = query.getDouble(2);
        value.plunge_rate = query.getDouble(3);
        value.spindle_speed = static_cast<int>(query.getInt(4));
        value.spindle_dir = static_cast<int>(query.getInt(5));
        value.stepdown = query.getDouble(6);
        value.stepover = query.getDouble(7);
        value.clear_stepover = query.getDouble(8);
        value.thread_depth = query.getDouble(9);
        value.thread_step_in = query.getDouble(10);
        value.laser_power = query.getDouble(11);
        value.laser_passes = static_cast<int>(query.getInt(12));
        value.laser_burn_rate = query.getDouble(13);
        value.line_width = query.getDouble(14);
        value.length_units = static_cast<int>(query.getInt(15));
        value.tool_number = static_cast<int>(query.getInt(16));
        value.laser_kerf = static_cast<int>(query.getInt(17));
        value.notes = query.getText(18);
        rows.cuttingData.emplace(value.id, std::move(value));
    }
    return true;
}

bool readEntities(Database& db, SourceRows& rows) {
    auto query = db.prepare(
        "SELECT id, material_id, machine_id, tool_geometry_id, tool_cutting_data_id "
        "FROM tool_entity");
    if (!query.isValid()) return false;
    while (query.step()) {
        VtdbToolEntity value;
        value.id = query.getText(0);
        value.material_id = query.isNull(1) ? "" : query.getText(1);
        value.machine_id = query.isNull(2) ? "" : query.getText(2);
        value.tool_geometry_id = query.getText(3);
        value.tool_cutting_data_id = query.getText(4);
        rows.entities.push_back(std::move(value));
    }
    return true;
}

bool readTree(Database& db, SourceRows& rows) {
    auto query = db.prepare(
        "SELECT id, parent_group_id, sibling_order, tool_geometry_id, name, notes, expanded "
        "FROM tool_tree_entry");
    if (!query.isValid()) return false;
    while (query.step()) {
        VtdbTreeEntry value;
        value.id = query.getText(0);
        value.parent_group_id = query.isNull(1) ? "" : query.getText(1);
        value.sibling_order = static_cast<int>(query.getInt(2));
        value.tool_geometry_id = query.isNull(3) ? "" : query.getText(3);
        value.name = query.isNull(4) ? "" : query.getText(4);
        value.notes = query.getText(5);
        value.expanded = static_cast<int>(query.getInt(6));
        rows.treeEntries.push_back(std::move(value));
    }
    return true;
}

} // namespace

class SupplierToolCatalog::Impl {
  public:
    Database db;
    Path sourcePath;
    std::vector<SupplierToolSummary> summaries;
    std::unordered_map<std::string, ToolBundle> bundles;
};

SupplierToolCatalog::SupplierToolCatalog() = default;
SupplierToolCatalog::~SupplierToolCatalog() = default;
SupplierToolCatalog::SupplierToolCatalog(SupplierToolCatalog&&) noexcept = default;
SupplierToolCatalog& SupplierToolCatalog::operator=(SupplierToolCatalog&&) noexcept = default;

SupplierToolCatalogOpenResult SupplierToolCatalog::open(const Path& sourcePath) {
    close();
    auto impl = std::make_unique<Impl>();
    if (!impl->db.openReadOnly(sourcePath))
        return failure(SupplierToolCatalogError::CannotOpen,
                       "Could not open the supplier tool database read-only.");

    const std::vector<std::string> tables = {
        "material", "machine", "tool_geometry", "tool_cutting_data",
        "tool_entity", "tool_tree_entry"};
    for (const auto& table : tables) {
        if (!hasTable(impl->db, table))
            return failure(SupplierToolCatalogError::InvalidSchema,
                           "Missing required .vtdb table: " + table);
    }

    const std::vector<std::pair<std::string, std::vector<std::string>>> required = {
        {"material", {"id", "name"}},
        {"machine", {"id", "name", "make", "model", "controller_type",
                     "dimensions_units", "max_width", "max_height", "support_rotary",
                     "support_tool_change", "has_laser_head"}},
        {"tool_geometry", {"id", "name_format", "notes", "tool_type", "units",
                           "diameter", "included_angle", "flat_diameter", "num_flutes",
                           "flute_length", "thread_pitch", "outline", "tip_radius",
                           "laser_watt", "custom_attributes", "tooth_size", "tooth_offset",
                           "neck_length", "tooth_height", "threaded_length"}},
        {"tool_cutting_data", {"id", "rate_units", "feed_rate", "plunge_rate",
                               "spindle_speed", "spindle_dir", "stepdown", "stepover",
                               "clear_stepover", "thread_depth", "thread_step_in",
                               "laser_power", "laser_passes", "laser_burn_rate",
                               "line_width", "length_units", "tool_number", "laser_kerf",
                               "notes"}},
        {"tool_entity", {"id", "material_id", "machine_id", "tool_geometry_id",
                         "tool_cutting_data_id"}},
        {"tool_tree_entry", {"id", "parent_group_id", "sibling_order",
                             "tool_geometry_id", "name", "notes", "expanded"}},
    };
    for (const auto& table : required) {
        if (!hasColumns(columnsFor(impl->db, table.first), table.second))
            return failure(SupplierToolCatalogError::UnsupportedSchema,
                           "Unsupported .vtdb schema in table: " + table.first);
    }

    SourceRows rows;
    if (!readMaterials(impl->db, rows) || !readMachines(impl->db, rows)
        || !readGeometries(impl->db, rows) || !readCuttingData(impl->db, rows)
        || !readEntities(impl->db, rows) || !readTree(impl->db, rows)) {
        return failure(SupplierToolCatalogError::QueryFailed,
                       "Could not read the supplier tool database.");
    }

    std::unordered_map<std::string, VtdbTreeEntry> entries;
    std::unordered_map<std::string, VtdbTreeEntry> leaves;
    for (const auto& entry : rows.treeEntries) {
        entries.emplace(entry.id, entry);
        if (!entry.tool_geometry_id.empty())
            leaves.emplace(entry.tool_geometry_id, entry);
    }
    std::unordered_map<std::string, std::vector<VtdbToolEntity>> entitiesByGeometry;
    for (const auto& entity : rows.entities) {
        if (!rows.geometries.count(entity.tool_geometry_id)
            || !rows.cuttingData.count(entity.tool_cutting_data_id)
            || (!entity.material_id.empty() && !rows.materials.count(entity.material_id))
            || (!entity.machine_id.empty() && !rows.machines.count(entity.machine_id))) {
            return failure(SupplierToolCatalogError::InvalidSchema,
                           "The supplier database contains a broken tool relationship.");
        }
        entitiesByGeometry[entity.tool_geometry_id].push_back(entity);
    }

    for (const auto& geometryId : rows.geometryOrder) {
        auto leafIt = leaves.find(geometryId);
        if (leafIt == leaves.end())
            return failure(SupplierToolCatalogError::InvalidSchema,
                           "A supplier tool is missing its library entry.");

        ToolBundle bundle;
        bundle.geometry = rows.geometries.at(geometryId);
        bundle.leaf = leafIt->second;
        std::unordered_set<std::string> visited;
        std::string parentId = bundle.leaf.parent_group_id;
        while (!parentId.empty()) {
            if (!visited.insert(parentId).second)
                return failure(SupplierToolCatalogError::InvalidSchema,
                               "The supplier tool category tree contains a cycle.");
            auto parent = entries.find(parentId);
            if (parent == entries.end() || !parent->second.tool_geometry_id.empty())
                return failure(SupplierToolCatalogError::InvalidSchema,
                               "The supplier tool category tree is incomplete.");
            bundle.ancestors.push_back(parent->second);
            parentId = parent->second.parent_group_id;
        }
        std::reverse(bundle.ancestors.begin(), bundle.ancestors.end());

        std::set<std::string> cuttingIds;
        std::set<std::string> materialIds;
        std::set<std::string> machineIds;
        bundle.entities = entitiesByGeometry[geometryId];
        for (const auto& entity : bundle.entities) {
            cuttingIds.insert(entity.tool_cutting_data_id);
            if (!entity.material_id.empty()) materialIds.insert(entity.material_id);
            if (!entity.machine_id.empty()) machineIds.insert(entity.machine_id);
        }
        for (const auto& id : cuttingIds) bundle.cuttingData.push_back(rows.cuttingData.at(id));
        for (const auto& id : materialIds) bundle.materials.push_back(rows.materials.at(id));
        for (const auto& id : machineIds) bundle.machines.push_back(rows.machines.at(id));

        SupplierToolSummary summary;
        summary.geometryId = geometryId;
        summary.treeEntryId = bundle.leaf.id;
        summary.displayName = bundle.leaf.name.empty()
            ? resolveToolNameFormat(bundle.geometry) : bundle.leaf.name;
        if (summary.displayName.empty()) summary.displayName = "(unnamed tool)";
        for (const auto& ancestor : bundle.ancestors) {
            if (!ancestor.name.empty()) summary.categoryPath.push_back(ancestor.name);
        }
        summary.toolType = bundle.geometry.tool_type;
        summary.units = bundle.geometry.units;
        summary.diameter = bundle.geometry.diameter;
        summary.numFlutes = bundle.geometry.num_flutes;
        summary.cuttingProfileCount = bundle.entities.size();
        impl->summaries.push_back(std::move(summary));
        impl->bundles.emplace(geometryId, std::move(bundle));
    }

    impl->sourcePath = sourcePath;
    m_impl = std::move(impl);
    return {};
}

void SupplierToolCatalog::close() { m_impl.reset(); }
bool SupplierToolCatalog::isOpen() const { return m_impl != nullptr; }

const Path& SupplierToolCatalog::path() const {
    static const Path empty;
    return m_impl ? m_impl->sourcePath : empty;
}

const std::vector<SupplierToolSummary>& SupplierToolCatalog::tools() const {
    static const std::vector<SupplierToolSummary> empty;
    return m_impl ? m_impl->summaries : empty;
}

const SupplierToolCatalog::ToolBundle* SupplierToolCatalog::findBundle(
    const std::string& geometryId) const {
    if (!m_impl) return nullptr;
    auto found = m_impl->bundles.find(geometryId);
    return found == m_impl->bundles.end() ? nullptr : &found->second;
}

} // namespace dw
