#include "tool_database.h"

#include "../utils/uuid.h"

namespace dw {

// --- Machine CRUD ---

bool ToolDatabase::insertMachine(const VtdbMachine& m) {
    std::string id = m.id.empty() ? uuid::generate() : m.id;
    auto stmt = m_db.prepare(R"(
        INSERT OR IGNORE INTO machine
            (id, name, make, model, controller_type, dimensions_units,
             max_width, max_height, support_rotary, support_tool_change, has_laser_head,
             spindle_power_watts, max_rpm, drive_type)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    if (!stmt.isValid()) return false;
    (void)stmt.bindText(1, id);
    (void)stmt.bindText(2, m.name);
    (void)stmt.bindText(3, m.make);
    (void)stmt.bindText(4, m.model);
    (void)stmt.bindText(5, m.controller_type);
    (void)stmt.bindInt(6, m.dimensions_units);
    (void)stmt.bindDouble(7, m.max_width);
    (void)stmt.bindDouble(8, m.max_height);
    (void)stmt.bindInt(9, m.support_rotary);
    (void)stmt.bindInt(10, m.support_tool_change);
    (void)stmt.bindInt(11, m.has_laser_head);
    (void)stmt.bindDouble(12, m.spindle_power_watts);
    (void)stmt.bindInt(13, m.max_rpm);
    (void)stmt.bindInt(14, static_cast<int>(m.drive_type));
    return stmt.execute();
}

bool ToolDatabase::updateMachine(const VtdbMachine& m) {
    auto stmt = m_db.prepare(R"(
        UPDATE machine SET
            name=?, make=?, model=?, controller_type=?, dimensions_units=?,
            max_width=?, max_height=?, support_rotary=?, support_tool_change=?,
            has_laser_head=?, spindle_power_watts=?, max_rpm=?, drive_type=?
        WHERE id=?
    )");
    if (!stmt.isValid()) return false;
    (void)stmt.bindText(1, m.name);
    (void)stmt.bindText(2, m.make);
    (void)stmt.bindText(3, m.model);
    (void)stmt.bindText(4, m.controller_type);
    (void)stmt.bindInt(5, m.dimensions_units);
    (void)stmt.bindDouble(6, m.max_width);
    (void)stmt.bindDouble(7, m.max_height);
    (void)stmt.bindInt(8, m.support_rotary);
    (void)stmt.bindInt(9, m.support_tool_change);
    (void)stmt.bindInt(10, m.has_laser_head);
    (void)stmt.bindDouble(11, m.spindle_power_watts);
    (void)stmt.bindInt(12, m.max_rpm);
    (void)stmt.bindInt(13, static_cast<int>(m.drive_type));
    (void)stmt.bindText(14, m.id);
    return stmt.execute();
}

static VtdbMachine rowToMachine(Statement& stmt) {
    VtdbMachine m;
    m.id = stmt.getText(0);
    m.name = stmt.getText(1);
    m.make = stmt.getText(2);
    m.model = stmt.getText(3);
    m.controller_type = stmt.getText(4);
    m.dimensions_units = static_cast<int>(stmt.getInt(5));
    m.max_width = stmt.getDouble(6);
    m.max_height = stmt.getDouble(7);
    m.support_rotary = static_cast<int>(stmt.getInt(8));
    m.support_tool_change = static_cast<int>(stmt.getInt(9));
    m.has_laser_head = static_cast<int>(stmt.getInt(10));
    m.spindle_power_watts = stmt.getDouble(11);
    m.max_rpm = static_cast<int>(stmt.getInt(12));
    m.drive_type = static_cast<DriveType>(stmt.getInt(13));
    return m;
}

static constexpr const char* kMachineSelect =
    "SELECT id, name, make, model, controller_type, dimensions_units, "
    "max_width, max_height, support_rotary, support_tool_change, has_laser_head, "
    "spindle_power_watts, max_rpm, drive_type FROM machine";

std::vector<VtdbMachine> ToolDatabase::findAllMachines() {
    std::vector<VtdbMachine> result;
    auto stmt = m_db.prepare(std::string(kMachineSelect) + " ORDER BY name");
    if (!stmt.isValid()) return result;
    while (stmt.step()) {
        result.push_back(rowToMachine(stmt));
    }
    return result;
}

std::optional<VtdbMachine> ToolDatabase::findMachineById(const std::string& id) {
    auto stmt = m_db.prepare(std::string(kMachineSelect) + " WHERE id = ?");
    if (!stmt.isValid() || !stmt.bindText(1, id) || !stmt.step())
        return std::nullopt;
    return rowToMachine(stmt);
}

// --- Material CRUD ---

bool ToolDatabase::insertMaterial(const VtdbMaterial& m) {
    std::string id = m.id.empty() ? uuid::generate() : m.id;
    auto stmt = m_db.prepare("INSERT OR IGNORE INTO material (id, name) VALUES (?, ?)");
    if (!stmt.isValid()) return false;
    (void)stmt.bindText(1, id);
    (void)stmt.bindText(2, m.name);
    return stmt.execute();
}

std::vector<VtdbMaterial> ToolDatabase::findAllMaterials() {
    std::vector<VtdbMaterial> result;
    auto stmt = m_db.prepare("SELECT id, name FROM material ORDER BY name");
    if (!stmt.isValid()) return result;
    while (stmt.step()) {
        VtdbMaterial m;
        m.id = stmt.getText(0);
        m.name = stmt.getText(1);
        result.push_back(m);
    }
    return result;
}

std::optional<VtdbMaterial> ToolDatabase::findMaterialById(const std::string& id) {
    auto stmt = m_db.prepare("SELECT id, name FROM material WHERE id = ?");
    if (!stmt.isValid() || !stmt.bindText(1, id) || !stmt.step())
        return std::nullopt;
    VtdbMaterial m;
    m.id = stmt.getText(0);
    m.name = stmt.getText(1);
    return m;
}

std::optional<VtdbMaterial> ToolDatabase::findMaterialByName(const std::string& name) {
    auto stmt = m_db.prepare("SELECT id, name FROM material WHERE name = ?");
    if (!stmt.isValid() || !stmt.bindText(1, name) || !stmt.step())
        return std::nullopt;
    VtdbMaterial m;
    m.id = stmt.getText(0);
    m.name = stmt.getText(1);
    return m;
}

} // namespace dw

