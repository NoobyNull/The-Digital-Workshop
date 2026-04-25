// Digital Workshop - Database Tests (in-memory SQLite)

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/schema.h"

namespace {

// Fixture: provides an in-memory database per test
class DatabaseTest : public ::testing::Test {
  protected:
    void SetUp() override { ASSERT_TRUE(m_db.open(":memory:")); }

    dw::Database m_db;
};

} // namespace

// --- Open / Close ---

TEST(Database, Open_InMemory) {
    dw::Database db;
    EXPECT_FALSE(db.isOpen());
    EXPECT_TRUE(db.open(":memory:"));
    EXPECT_TRUE(db.isOpen());
}

TEST(Database, Close) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(db.isOpen());
    db.close();
    EXPECT_FALSE(db.isOpen());
}

// --- Execute ---

TEST_F(DatabaseTest, Execute_CreateTable) {
    EXPECT_TRUE(m_db.execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)"));
}

TEST_F(DatabaseTest, Execute_InvalidSQL) {
    EXPECT_FALSE(m_db.execute("NOT VALID SQL"));
    EXPECT_FALSE(m_db.lastError().empty());
}

// --- Prepared Statements ---

TEST_F(DatabaseTest, PrepareAndBind_InsertAndQuery) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, value REAL)"));

    // Insert
    auto insert = m_db.prepare("INSERT INTO items (name, value) VALUES (?, ?)");
    ASSERT_TRUE(insert.isValid());
    ASSERT_TRUE(insert.bindText(1, "widget"));
    ASSERT_TRUE(insert.bindDouble(2, 3.14));
    EXPECT_TRUE(insert.execute());

    EXPECT_EQ(m_db.lastInsertId(), 1);
    EXPECT_EQ(m_db.changesCount(), 1);

    // Query
    auto query = m_db.prepare("SELECT id, name, value FROM items WHERE name = ?");
    ASSERT_TRUE(query.bindText(1, "widget"));
    ASSERT_TRUE(query.step());

    EXPECT_EQ(query.getInt(0), 1);
    EXPECT_EQ(query.getText(1), "widget");
    EXPECT_NEAR(query.getDouble(2), 3.14, 0.001);
    EXPECT_EQ(query.columnCount(), 3);
}

TEST_F(DatabaseTest, PrepareAndBind_Null) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));

    auto insert = m_db.prepare("INSERT INTO items (name) VALUES (?)");
    ASSERT_TRUE(insert.bindNull(1));
    EXPECT_TRUE(insert.execute());

    auto query = m_db.prepare("SELECT name FROM items WHERE id = 1");
    ASSERT_TRUE(query.step());
    EXPECT_TRUE(query.isNull(0));
}

TEST_F(DatabaseTest, PrepareAndBind_Blob) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE blobs (id INTEGER PRIMARY KEY, data BLOB)"));

    dw::ByteBuffer blob = {0xDE, 0xAD, 0xBE, 0xEF};
    auto insert = m_db.prepare("INSERT INTO blobs (data) VALUES (?)");
    ASSERT_TRUE(insert.bindBlob(1, blob.data(), static_cast<int>(blob.size())));
    EXPECT_TRUE(insert.execute());

    auto query = m_db.prepare("SELECT data FROM blobs WHERE id = 1");
    ASSERT_TRUE(query.step());
    auto result = query.getBlob(0);
    EXPECT_EQ(result, blob);
}

TEST_F(DatabaseTest, Statement_Reset) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));

    auto insert = m_db.prepare("INSERT INTO items (name) VALUES (?)");
    ASSERT_TRUE(insert.bindText(1, "first"));
    EXPECT_TRUE(insert.execute());

    insert.reset();
    ASSERT_TRUE(insert.bindText(1, "second"));
    EXPECT_TRUE(insert.execute());

    auto query = m_db.prepare("SELECT COUNT(*) FROM items");
    ASSERT_TRUE(query.step());
    EXPECT_EQ(query.getInt(0), 2);
}

TEST_F(DatabaseTest, Statement_ColumnName) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, value REAL)"));

    auto query = m_db.prepare("SELECT id, name, value FROM items");
    EXPECT_EQ(query.columnName(0), "id");
    EXPECT_EQ(query.columnName(1), "name");
    EXPECT_EQ(query.columnName(2), "value");
}

// --- Transactions ---

TEST_F(DatabaseTest, Transaction_Commit) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));

    {
        dw::Transaction txn(m_db);
        ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('a')"));
        ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('b')"));
        EXPECT_TRUE(txn.commit());
    }

    auto query = m_db.prepare("SELECT COUNT(*) FROM items");
    ASSERT_TRUE(query.step());
    EXPECT_EQ(query.getInt(0), 2);
}

TEST_F(DatabaseTest, Transaction_Rollback) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));

    {
        dw::Transaction txn(m_db);
        ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('a')"));
        ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('b')"));
        txn.rollback();
    }

    auto query = m_db.prepare("SELECT COUNT(*) FROM items");
    ASSERT_TRUE(query.step());
    EXPECT_EQ(query.getInt(0), 0);
}

TEST_F(DatabaseTest, Transaction_AutoRollbackOnDestruction) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));

    {
        dw::Transaction txn(m_db);
        ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('a')"));
        // No commit — destructor should rollback
    }

    auto query = m_db.prepare("SELECT COUNT(*) FROM items");
    ASSERT_TRUE(query.step());
    EXPECT_EQ(query.getInt(0), 0);
}

// --- Multiple inserts ---

TEST_F(DatabaseTest, MultipleInserts_LastInsertId) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));

    ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('a')"));
    EXPECT_EQ(m_db.lastInsertId(), 1);

    ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('b')"));
    EXPECT_EQ(m_db.lastInsertId(), 2);

    ASSERT_TRUE(m_db.execute("INSERT INTO items (name) VALUES ('c')"));
    EXPECT_EQ(m_db.lastInsertId(), 3);
}

// --- Statement move semantics ---

TEST_F(DatabaseTest, Statement_MoveConstruct) {
    ASSERT_TRUE(m_db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY)"));

    auto stmt1 = m_db.prepare("SELECT * FROM items");
    ASSERT_TRUE(stmt1.isValid());

    dw::Statement stmt2(std::move(stmt1));
    EXPECT_TRUE(stmt2.isValid());
    EXPECT_FALSE(stmt1.isValid()); // NOLINT: testing moved-from state
}

// --- Schema Migration v8 to v9 ---

TEST(Database, SchemaMigration_v8_to_v9) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));

    // Manually create a v8-like schema with the projects table (no notes column)
    ASSERT_TRUE(db.execute("CREATE TABLE schema_version (version INTEGER NOT NULL)"));
    ASSERT_TRUE(db.execute("INSERT INTO schema_version (version) VALUES (8)"));
    ASSERT_TRUE(db.execute(R"(
        CREATE TABLE projects (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            description TEXT DEFAULT '',
            file_path TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            modified_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )"));
    // Need gcode_files table for FKs
    ASSERT_TRUE(db.execute(R"(
        CREATE TABLE gcode_files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            hash TEXT UNIQUE NOT NULL,
            name TEXT NOT NULL,
            file_path TEXT NOT NULL,
            file_size INTEGER DEFAULT 0,
            bounds_min_x REAL DEFAULT 0, bounds_min_y REAL DEFAULT 0, bounds_min_z REAL DEFAULT 0,
            bounds_max_x REAL DEFAULT 0, bounds_max_y REAL DEFAULT 0, bounds_max_z REAL DEFAULT 0,
            total_distance REAL DEFAULT 0, estimated_time REAL DEFAULT 0,
            feed_rates TEXT DEFAULT '[]', tool_numbers TEXT DEFAULT '[]',
            imported_at TEXT DEFAULT CURRENT_TIMESTAMP, thumbnail_path TEXT
        )
    )"));

    // Run schema initialization (should trigger v8->v9 migration)
    ASSERT_TRUE(dw::Schema::initialize(db));

    // Verify version is now current
    EXPECT_EQ(dw::Schema::getVersion(db), 17);

    // Verify project_gcode table exists
    auto stmt1 = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='project_gcode'");
    EXPECT_TRUE(stmt1.step());

    // Verify cut_plans table exists
    auto stmt2 = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='cut_plans'");
    EXPECT_TRUE(stmt2.step());

    // Verify notes column was added to projects
    ASSERT_TRUE(db.execute("INSERT INTO projects (name, notes) VALUES ('test', 'some notes')"));
    auto stmt3 = db.prepare("SELECT notes FROM projects WHERE name = 'test'");
    ASSERT_TRUE(stmt3.step());
    EXPECT_EQ(stmt3.getText(0), "some notes");
}
