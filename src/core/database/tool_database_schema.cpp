#include "tool_database.h"

#include "../utils/log.h"

namespace dw {
namespace {

bool addMachineColumnIfMissing(Database& db,
                               const char* column,
                               const char* definition) {
    auto check = db.prepare(
        "SELECT 1 FROM pragma_table_info('machine') WHERE name = ? LIMIT 1");
    if (!check.isValid() || !check.bindText(1, column))
        return false;
    if (check.step())
        return true;
    return db.execute(std::string("ALTER TABLE machine ADD COLUMN ") + definition);
}

} // namespace

bool ToolDatabase::open(const Path& path) {
    if (!m_db.open(path.string())) {
        log::errorf("ToolDatabase", "Failed to open: %s", path.string().c_str());
        return false;
    }

    auto check = m_db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='version'");
    const bool hasSchema = check.step();
    if (!hasSchema && !initializeSchema(m_db)) {
        log::error("ToolDatabase", "Failed to initialize schema");
        m_db.close();
        return false;
    }
    if (!migrateSchema(m_db)) {
        log::error("ToolDatabase", "Failed to migrate schema");
        m_db.close();
        return false;
    }

    log::infof("ToolDatabase", "Opened: %s", path.string().c_str());
    return true;
}

bool ToolDatabase::migrateSchema(Database& db) {
    if (!db.isOpen())
        return false;

    Transaction txn(db);
    if (!addMachineColumnIfMissing(db,
                                   "spindle_power_watts",
                                   "spindle_power_watts REAL DEFAULT 0") ||
        !addMachineColumnIfMissing(db,
                                   "max_rpm",
                                   "max_rpm INTEGER DEFAULT 24000") ||
        !addMachineColumnIfMissing(db,
                                   "drive_type",
                                   "drive_type INTEGER DEFAULT 0")) {
        return false;
    }
    return txn.commit();
}

bool ToolDatabase::initializeSchema(Database& db) {
    if (!db.isOpen())
        return false;

    Transaction txn(db);

    // Exact DDL from real Vectric .vtdb files, with Digital Workshop's three
    // backward-compatible machine capability columns included for new files.
    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "version" (
            "version" INTEGER NOT NULL UNIQUE,
            PRIMARY KEY("version")
        )
    )"))
        return false;

    if (!db.execute("INSERT OR IGNORE INTO version (version) VALUES (1)"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "migration" (
            "version"    INTEGER NOT NULL,
            "subversion" INTEGER NOT NULL,
            "name"       TEXT NOT NULL,
            "checksum"   TEXT NOT NULL,
            PRIMARY KEY("version","subversion")
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "material" (
            "id"   TEXT NOT NULL UNIQUE PRIMARY KEY,
            "name" TEXT NOT NULL UNIQUE
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "machine" (
            "id"                  TEXT NOT NULL UNIQUE PRIMARY KEY,
            "name"                TEXT NOT NULL UNIQUE,
            "make"                TEXT,
            "model"               TEXT,
            "controller_type"     TEXT,
            "dimensions_units"    INTEGER,
            "max_width"           REAL,
            "max_height"          REAL,
            "support_rotary"      INTEGER,
            "support_tool_change" INTEGER,
            "has_laser_head"      INTEGER,
            "spindle_power_watts" REAL DEFAULT 0,
            "max_rpm"             INTEGER DEFAULT 24000,
            "drive_type"          INTEGER DEFAULT 0
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "tool_geometry" (
            "id"                TEXT NOT NULL UNIQUE PRIMARY KEY,
            "name_format"       TEXT NOT NULL,
            "notes"             TEXT,
            "tool_type"         INTEGER NOT NULL,
            "units"             INTEGER NOT NULL,
            "diameter"          REAL,
            "included_angle"    REAL,
            "flat_diameter"     REAL,
            "num_flutes"        INTEGER,
            "flute_length"      REAL,
            "thread_pitch"      REAL,
            "outline"           BLOB,
            "tip_radius"        REAL,
            "laser_watt"        INTEGER,
            "custom_attributes" TEXT,
            "tooth_size"        REAL,
            "tooth_offset"      REAL,
            "neck_length"       REAL,
            "tooth_height"      REAL,
            "threaded_length"   REAL
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "tool_cutting_data" (
            "id"              TEXT NOT NULL UNIQUE PRIMARY KEY,
            "rate_units"      INTEGER NOT NULL,
            "feed_rate"       REAL,
            "plunge_rate"     REAL,
            "spindle_speed"   INTEGER,
            "spindle_dir"     INTEGER,
            "stepdown"        REAL,
            "stepover"        REAL,
            "clear_stepover"  REAL,
            "thread_depth"    REAL,
            "thread_step_in"  REAL,
            "laser_power"     REAL,
            "laser_passes"    INTEGER,
            "laser_burn_rate" REAL,
            "line_width"      REAL,
            "length_units"    INTEGER NOT NULL DEFAULT 0,
            "tool_number"     INTEGER,
            "laser_kerf"      INTEGER,
            "notes"           TEXT
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "tool_entity" (
            "id"                   TEXT NOT NULL UNIQUE,
            "material_id"          TEXT,
            "machine_id"           TEXT,
            "tool_geometry_id"     TEXT,
            "tool_cutting_data_id" TEXT NOT NULL,
            PRIMARY KEY("tool_geometry_id","material_id","machine_id"),
            FOREIGN KEY("material_id")          REFERENCES "material"("id"),
            FOREIGN KEY("machine_id")           REFERENCES "machine"("id"),
            FOREIGN KEY("tool_geometry_id")     REFERENCES "tool_geometry"("id"),
            FOREIGN KEY("tool_cutting_data_id") REFERENCES "tool_cutting_data"("id")
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "tool_tree_entry" (
            "id"               TEXT NOT NULL UNIQUE,
            "parent_group_id"  TEXT,
            "sibling_order"    INTEGER NOT NULL,
            "tool_geometry_id" TEXT UNIQUE,
            "name"             TEXT,
            "notes"            TEXT,
            "expanded"         INTEGER,
            PRIMARY KEY("id","parent_group_id","sibling_order"),
            FOREIGN KEY("tool_geometry_id") REFERENCES "tool_geometry"("id"),
            FOREIGN KEY("parent_group_id")  REFERENCES "tool_tree_entry"("id")
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "tool_name_format" (
            "id"        TEXT NOT NULL,
            "tool_type" INTEGER NOT NULL UNIQUE,
            "format"    TEXT NOT NULL,
            PRIMARY KEY("id","tool_type")
        )
    )"))
        return false;

    if (!db.execute(R"(
        CREATE TABLE IF NOT EXISTS "upload_data" (
            "id"            INTEGER NOT NULL UNIQUE PRIMARY KEY,
            "date_uploaded" INTEGER NOT NULL
        )
    )"))
        return false;

    if (!txn.commit()) {
        log::error("ToolDatabase", "Failed to commit schema");
        return false;
    }
    return true;
}

} // namespace dw
