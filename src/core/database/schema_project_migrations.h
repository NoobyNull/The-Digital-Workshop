#pragma once

namespace dw {

class Database;

// Project-session schema changes are kept separate from the legacy schema
// coordinator so lifecycle persistence can evolve without growing it again.
[[nodiscard]] bool migrateProjectSessionSchema(Database& database, int fromVersion);

} // namespace dw
