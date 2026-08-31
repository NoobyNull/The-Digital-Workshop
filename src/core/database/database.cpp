#include "database.h"

#include <cstring>

#include <sqlite3.h>

#include "../utils/log.h"

namespace dw {
namespace {

std::string immutableSqliteUri(const Path& path) {
    constexpr char hex[] = "0123456789ABCDEF";
    const std::string source = path.generic_string();
    std::string uri = "file:";
    uri.reserve(source.size() + 32);
    for (const unsigned char character : source) {
        const bool unreserved = (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '/' || character == ':' || character == '-'
            || character == '.' || character == '_' || character == '~';
        if (unreserved) {
            uri.push_back(static_cast<char>(character));
        } else {
            uri.push_back('%');
            uri.push_back(hex[character >> 4]);
            uri.push_back(hex[character & 0x0f]);
        }
    }
    uri += "?mode=ro&immutable=1";
    return uri;
}

} // namespace

// Statement implementation

Statement::Statement(sqlite3_stmt* stmt) : m_stmt(stmt) {}

Statement::~Statement() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
    }
}

Statement::Statement(Statement&& other) noexcept : m_stmt(other.m_stmt) {
    other.m_stmt = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
        }
        m_stmt = other.m_stmt;
        other.m_stmt = nullptr;
    }
    return *this;
}

bool Statement::bindInt(int index, i64 value) {
    return sqlite3_bind_int64(m_stmt, index, value) == SQLITE_OK;
}

bool Statement::bindDouble(int index, f64 value) {
    return sqlite3_bind_double(m_stmt, index, value) == SQLITE_OK;
}

bool Statement::bindText(int index, const std::string& value) {
    return sqlite3_bind_text(
               m_stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) ==
           SQLITE_OK;
}

bool Statement::bindBlob(int index, const void* data, int size) {
    return sqlite3_bind_blob(m_stmt, index, data, size, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool Statement::bindNull(int index) {
    return sqlite3_bind_null(m_stmt, index) == SQLITE_OK;
}

bool Statement::step() {
    int result = sqlite3_step(m_stmt);
    return result == SQLITE_ROW;
}

bool Statement::execute() {
    int result = sqlite3_step(m_stmt);
    return result == SQLITE_DONE;
}

void Statement::reset() {
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
}

i64 Statement::getInt(int column) const {
    return sqlite3_column_int64(m_stmt, column);
}

f64 Statement::getDouble(int column) const {
    return sqlite3_column_double(m_stmt, column);
}

std::string Statement::getText(int column) const {
    const unsigned char* text = sqlite3_column_text(m_stmt, column);
    if (text) {
        return std::string(reinterpret_cast<const char*>(text));
    }
    return "";
}

ByteBuffer Statement::getBlob(int column) const {
    const void* data = sqlite3_column_blob(m_stmt, column);
    int size = sqlite3_column_bytes(m_stmt, column);
    if (data && size > 0) {
        ByteBuffer buffer(static_cast<usize>(size));
        std::memcpy(buffer.data(), data, static_cast<usize>(size));
        return buffer;
    }
    return {};
}

bool Statement::isNull(int column) const {
    return sqlite3_column_type(m_stmt, column) == SQLITE_NULL;
}

int Statement::columnCount() const {
    return sqlite3_column_count(m_stmt);
}

std::string Statement::columnName(int column) const {
    const char* name = sqlite3_column_name(m_stmt, column);
    return name ? name : "";
}

// Database implementation

Database::~Database() {
    close();
}

bool Database::open(const Path& path) {
    return openWithFlags(path, 0);
}

bool Database::openReadOnly(const Path& path) {
    if (m_db) {
        close();
    }

    const std::string uri = immutableSqliteUri(path);
    const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_URI;
    const int result = sqlite3_open_v2(uri.c_str(), &m_db, flags, nullptr);
    if (result != SQLITE_OK) {
        const char* error = m_db ? sqlite3_errmsg(m_db) : "unknown SQLite error";
        log::errorf("Database", "Failed to open read-only: %s", error);
        if (m_db)
            sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    sqlite3_busy_timeout(m_db, 5000);
    if (!execute("PRAGMA query_only = ON")) {
        close();
        return false;
    }
    if (!execute("PRAGMA foreign_keys = ON")) {
        log::warning("Database", "Failed to enable foreign keys on read-only connection");
    }

    log::infof("Database", "Opened read-only: %s", path.string().c_str());
    return true;
}

bool Database::openWithFlags(const Path& path, int extraFlags) {
    if (m_db) {
        close();
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | extraFlags;
    int result = sqlite3_open_v2(path.string().c_str(), &m_db, flags, nullptr);
    if (result != SQLITE_OK) {
        log::errorf("Database", "Failed to open: %s", sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // Set busy timeout to 5 seconds for better multi-threaded behavior
    sqlite3_busy_timeout(m_db, 5000);

    // Enable foreign keys
    if (!execute("PRAGMA foreign_keys = ON")) {
        log::warning("Database", "Failed to enable foreign keys");
    }

    // Use WAL mode for better concurrency
    if (!execute("PRAGMA journal_mode = WAL")) {
        log::warning("Database", "Failed to set WAL mode");
    }

    // Set synchronous mode to NORMAL for better performance with WAL
    if (!execute("PRAGMA synchronous = NORMAL")) {
        log::warning("Database", "Failed to set synchronous mode");
    }

    log::infof("Database", "Opened: %s", path.string().c_str());
    return true;
}

void Database::close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        log::debug("Database", "Closed");
    }
}

bool Database::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int result = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);

    if (result != SQLITE_OK) {
        log::errorf("Database", "SQL error: %s\nQuery: %s", errMsg, sql.c_str());
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

Statement Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int result =
        sqlite3_prepare_v2(m_db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);

    if (result != SQLITE_OK) {
        log::errorf("Database",
                    "Failed to prepare statement: %s\nQuery: %s",
                    sqlite3_errmsg(m_db),
                    sql.c_str());
        return Statement();
    }

    return Statement(stmt);
}

bool Database::beginTransaction() {
    return execute("BEGIN TRANSACTION");
}

bool Database::commit() {
    return execute("COMMIT");
}

bool Database::rollback() {
    return execute("ROLLBACK");
}

bool Database::enableExtensionLoading() {
    if (!m_db)
        return false;
    int rc = sqlite3_enable_load_extension(m_db, 1);
    if (rc != SQLITE_OK) {
        log::errorf("Database", "Failed to enable extension loading: %s", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

void Database::disableExtensionLoading() {
    if (m_db) {
        sqlite3_enable_load_extension(m_db, 0);
    }
}

bool Database::loadExtension(const std::string& path, std::string& error) {
    if (!m_db) {
        error = "Database not open";
        return false;
    }
    char* errMsg = nullptr;
    int rc = sqlite3_load_extension(m_db, path.c_str(), nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        error = errMsg ? errMsg : "Unknown error loading extension";
        if (errMsg)
            sqlite3_free(errMsg);
        return false;
    }
    return true;
}

i64 Database::lastInsertId() const {
    return sqlite3_last_insert_rowid(m_db);
}

int Database::changesCount() const {
    return sqlite3_changes(m_db);
}

std::string Database::lastError() const {
    return sqlite3_errmsg(m_db);
}

// Transaction implementation

Transaction::Transaction(Database& db) : m_db(db) {
    m_started = m_db.beginTransaction();
    if (!m_started) {
        log::error("Transaction", "Failed to begin transaction");
    }
}

Transaction::~Transaction() {
    if (m_started && !m_finished) {
        (void)m_db.rollback();
    }
}

bool Transaction::commit() {
    if (!m_started || m_finished)
        return m_committed;
    m_committed = m_db.commit();
    m_finished = m_committed;
    return m_committed;
}

bool Transaction::rollback() {
    if (!m_started)
        return false;
    if (m_finished)
        return !m_committed;
    const bool rolledBack = m_db.rollback();
    m_finished = rolledBack;
    return rolledBack;
}

} // namespace dw
