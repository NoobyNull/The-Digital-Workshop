#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../types.h"

struct sqlite3;
struct sqlite3_stmt;

namespace dw {

// Forward declarations
class Database;

// RAII wrapper for prepared statement
class Statement {
  public:
    Statement() = default;
    Statement(sqlite3_stmt* stmt);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    // Bind parameters (1-indexed)
    [[nodiscard]] bool bindInt(int index, i64 value);
    [[nodiscard]] bool bindDouble(int index, f64 value);
    [[nodiscard]] bool bindText(int index, const std::string& value);
    [[nodiscard]] bool bindBlob(int index, const void* data, int size);
    [[nodiscard]] bool bindNull(int index);

    // Execute and step
    [[nodiscard]] bool step();    // Returns true if there's a row (SQLITE_ROW)
    [[nodiscard]] bool execute(); // Returns true if successful (SQLITE_DONE)
    void reset();                 // Reset for reuse

    // Get column values (0-indexed)
    i64 getInt(int column) const;
    f64 getDouble(int column) const;
    std::string getText(int column) const;
    ByteBuffer getBlob(int column) const;
    bool isNull(int column) const;

    // Get column count and names
    int columnCount() const;
    std::string columnName(int column) const;

    bool isValid() const { return m_stmt != nullptr; }

  private:
    sqlite3_stmt* m_stmt = nullptr;
};

// Database connection wrapper
class Database {
  public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Open/close database
    [[nodiscard]] bool open(const Path& path);
    // Open an existing database without creating or changing the source file.
    // Read-only connections intentionally skip WAL and synchronous pragmas.
    [[nodiscard]] bool openReadOnly(const Path& path);
    [[nodiscard]] bool openWithFlags(const Path& path, int extraFlags);
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // Execute SQL (for simple queries without results)
    [[nodiscard]] bool execute(const std::string& sql);

    // Prepare statement for queries with results or parameters
    Statement prepare(const std::string& sql);

    // Transaction support
    [[nodiscard]] bool beginTransaction();
    [[nodiscard]] bool commit();
    [[nodiscard]] bool rollback();

    // Extension loading
    // Enable loading of SQLite extensions (must call before loadExtension)
    bool enableExtensionLoading();

    // Disable loading of SQLite extensions (call after loading for security)
    void disableExtensionLoading();

    // Load a SQLite extension from the given path
    // Returns true on success, sets error on failure
    bool loadExtension(const std::string& path, std::string& error);

    // Get raw sqlite3 handle (needed for advanced operations)
    sqlite3* handle() const { return m_db; }

    // Utility
    i64 lastInsertId() const;
    int changesCount() const;
    std::string lastError() const;

  private:
    sqlite3* m_db = nullptr;
};

// Scoped transaction (RAII)
class Transaction {
  public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    [[nodiscard]] bool started() const { return m_started; }
    [[nodiscard]] bool commit();
    bool rollback();

  private:
    Database& m_db;
    bool m_started = false;
    bool m_finished = false;
    bool m_committed = false;
};

} // namespace dw
