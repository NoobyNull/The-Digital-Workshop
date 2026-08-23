// Digital Workshop - Schema Tests

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/schema.h"

TEST(Schema, Initialize_FreshDatabase) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));

    EXPECT_FALSE(dw::Schema::isInitialized(db));
    EXPECT_TRUE(dw::Schema::initialize(db));
    EXPECT_TRUE(dw::Schema::isInitialized(db));
}

TEST(Schema, GetVersion_AfterInit) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    EXPECT_EQ(dw::Schema::getVersion(db), 18);
}

TEST(Schema, GetVersion_BeforeInit) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));

    EXPECT_EQ(dw::Schema::getVersion(db), 0);
}

TEST(Schema, DoubleInit_Idempotent) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));

    EXPECT_TRUE(dw::Schema::initialize(db));
    EXPECT_TRUE(dw::Schema::initialize(db));
    EXPECT_EQ(dw::Schema::getVersion(db), 18);
}

TEST(Schema, TablesCreated) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    // Verify models table exists
    auto stmt1 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='models'");
    EXPECT_TRUE(stmt1.step());

    // Verify projects table exists
    auto stmt2 =
        db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='projects'");
    EXPECT_TRUE(stmt2.step());

    // Verify junction table exists
    auto stmt3 =
        db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='project_models'");
    EXPECT_TRUE(stmt3.step());

    // Verify G-code tables exist
    auto stmt4 =
        db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='gcode_files'");
    EXPECT_TRUE(stmt4.step());

    auto stmt5 =
        db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='operation_groups'");
    EXPECT_TRUE(stmt5.step());

    auto stmt6 = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='gcode_group_members'");
    EXPECT_TRUE(stmt6.step());

    auto stmt7 =
        db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='gcode_templates'");
    EXPECT_TRUE(stmt7.step());

    auto stmt8 = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='project_open_items'");
    EXPECT_TRUE(stmt8.step());
}

TEST(Schema, IndexesCreated) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    auto stmt =
        db.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name='idx_models_hash'");
    EXPECT_TRUE(stmt.step());
}

TEST(Schema, TagStatusColumn_Exists) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    // Verify tag_status column exists with default 0
    ASSERT_TRUE(db.execute(
        "INSERT INTO models (hash, name, file_path, file_format) "
        "VALUES ('abc123', 'test', '/tmp/test.stl', 'stl')"));
    auto stmt = db.prepare("SELECT tag_status FROM models WHERE hash = 'abc123'");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.getInt(0), 0);
}

TEST(Schema, TagStatusIndex_Exists) {
    dw::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(dw::Schema::initialize(db));

    auto stmt = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='index' AND name='idx_models_tag_status'");
    EXPECT_TRUE(stmt.step());
}
