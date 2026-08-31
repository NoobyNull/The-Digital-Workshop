#include "schema_project_migrations.h"

#include "../utils/log.h"
#include "database.h"

namespace dw {

bool migrateProjectSessionSchema(Database& database, int fromVersion) {
    if (fromVersion < 17) {
        if (!database.execute(R"(
                CREATE TABLE IF NOT EXISTS project_open_items (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    project_id INTEGER NOT NULL,
                    item_type TEXT NOT NULL,
                    source_table TEXT DEFAULT '',
                    source_id INTEGER DEFAULT NULL,
                    source_key TEXT DEFAULT '',
                    parent_item_id INTEGER DEFAULT NULL,
                    status TEXT NOT NULL DEFAULT 'planned',
                    display_name TEXT NOT NULL DEFAULT '',
                    intent_json TEXT NOT NULL DEFAULT '{}',
                    snapshot_json TEXT NOT NULL DEFAULT '{}',
                    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
                    modified_at TEXT DEFAULT CURRENT_TIMESTAMP,
                    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
                    FOREIGN KEY (parent_item_id) REFERENCES project_open_items(id)
                        ON DELETE CASCADE
                )
            )")) {
            return false;
        }
        (void)database.execute("CREATE INDEX IF NOT EXISTS idx_project_open_items_project ON "
                               "project_open_items(project_id)");
        (void)database.execute("CREATE INDEX IF NOT EXISTS idx_project_open_items_parent ON "
                               "project_open_items(parent_item_id)");
        (void)database.execute("CREATE INDEX IF NOT EXISTS idx_project_open_items_source ON "
                               "project_open_items(project_id, source_table, source_id)");
        (void)database.execute("CREATE INDEX IF NOT EXISTS idx_project_open_items_source_key ON "
                               "project_open_items(project_id, source_key)");
        log::info("Schema", "v17: Added project_open_items table");
    }

    if (fromVersion < 18) {
        if (!database.execute(
                "ALTER TABLE projects ADD COLUMN temporary INTEGER NOT NULL DEFAULT 0")) {
            return false;
        }
        log::info("Schema", "v18: Persisted temporary project ownership");
    }
    return true;
}

} // namespace dw
