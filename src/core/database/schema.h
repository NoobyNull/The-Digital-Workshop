#pragma once

#include "database.h"

namespace dw {

// Database schema initialization
class Schema {
  public:
    // Initialize schema (creates tables if they don't exist)
    [[nodiscard]] static bool initialize(Database& db);

    // Check if schema is initialized
    [[nodiscard]] static bool isInitialized(Database& db);

    // Get schema version
    static int getVersion(Database& db);

  private:
    static constexpr int CURRENT_VERSION = 17;

    static bool migrate(Database& db, int fromVersion);

    static bool createTables(Database& db);
    static bool setVersion(Database& db, int version);
};

} // namespace dw
