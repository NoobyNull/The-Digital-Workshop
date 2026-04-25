// Tests for MacroManager -- SQLite CRUD, built-in macros, parseLines, reorder
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>

#include "core/cnc/macro_manager.h"

using namespace dw;

namespace {

// Helper to create a unique temp DB path per test
std::string tempDbPath() {
    static std::atomic<unsigned long long> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    std::ostringstream name;
    name << "test_macro_manager_" << now << "_" << counter.fetch_add(1) << ".db";
    return (std::filesystem::temp_directory_path() / name.str()).string();
}

class MacroManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_dbPath = tempDbPath();
        m_mgr = std::make_unique<MacroManager>(m_dbPath);
    }

    void TearDown() override {
        m_mgr.reset();
        std::filesystem::remove(m_dbPath);
    }

    std::string m_dbPath;
    std::unique_ptr<MacroManager> m_mgr;
};

} // namespace

// --- Schema creation ---

TEST_F(MacroManagerTest, SchemaCreation) {
    // If we get here without throwing, schema was created successfully
    auto all = m_mgr->getAll();
    EXPECT_TRUE(all.empty());
}

// --- Add and retrieve ---

TEST_F(MacroManagerTest, AddAndRetrieveById) {
    Macro m;
    m.name = "Test Macro";
    m.gcode = "G0 X10 Y10";
    m.shortcut = "Ctrl+T";
    m.sortOrder = 5;

    int id = m_mgr->addMacro(m);
    EXPECT_GT(id, 0);

    auto retrieved = m_mgr->getById(id);
    EXPECT_EQ(retrieved.id, id);
    EXPECT_EQ(retrieved.name, "Test Macro");
    EXPECT_EQ(retrieved.gcode, "G0 X10 Y10");
    EXPECT_EQ(retrieved.shortcut, "Ctrl+T");
    EXPECT_EQ(retrieved.sortOrder, 5);
    EXPECT_FALSE(retrieved.builtIn);
}

// --- Update ---

TEST_F(MacroManagerTest, UpdateMacro) {
    Macro m;
    m.name = "Original";
    m.gcode = "G0 X0";
    int id = m_mgr->addMacro(m);

    auto updated = m_mgr->getById(id);
    updated.name = "Updated Name";
    updated.gcode = "G1 X50 F500";
    m_mgr->updateMacro(updated);

    auto check = m_mgr->getById(id);
    EXPECT_EQ(check.name, "Updated Name");
    EXPECT_EQ(check.gcode, "G1 X50 F500");
}

// --- Delete non-builtin ---

TEST_F(MacroManagerTest, DeleteNonBuiltin) {
    Macro m;
    m.name = "Deletable";
    m.gcode = "G0 X0";
    int id = m_mgr->addMacro(m);

    m_mgr->deleteMacro(id);

    EXPECT_THROW(m_mgr->getById(id), std::runtime_error);
}

// --- Delete builtin throws ---

TEST_F(MacroManagerTest, DeleteBuiltinThrows) {
    Macro m;
    m.name = "Built-in Test";
    m.gcode = "$H";
    m.builtIn = true;
    int id = m_mgr->addMacro(m);

    EXPECT_THROW(m_mgr->deleteMacro(id), std::runtime_error);

    // Verify it still exists
    auto check = m_mgr->getById(id);
    EXPECT_EQ(check.name, "Built-in Test");
}

// --- getAll returns sorted by sortOrder ---

TEST_F(MacroManagerTest, GetAllSortedBySortOrder) {
    Macro m1;
    m1.name = "Third";
    m1.gcode = "G0";
    m1.sortOrder = 3;
    m_mgr->addMacro(m1);

    Macro m2;
    m2.name = "First";
    m2.gcode = "G0";
    m2.sortOrder = 1;
    m_mgr->addMacro(m2);

    Macro m3;
    m3.name = "Second";
    m3.gcode = "G0";
    m3.sortOrder = 2;
    m_mgr->addMacro(m3);

    auto all = m_mgr->getAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].name, "First");
    EXPECT_EQ(all[1].name, "Second");
    EXPECT_EQ(all[2].name, "Third");
}

// --- ensureBuiltIns creates 3 built-ins ---

TEST_F(MacroManagerTest, EnsureBuiltInsCreatesThree) {
    m_mgr->ensureBuiltIns();

    auto all = m_mgr->getAll();
    ASSERT_EQ(all.size(), 3u);

    EXPECT_EQ(all[0].name, "Homing Cycle");
    EXPECT_TRUE(all[0].builtIn);
    EXPECT_EQ(all[0].gcode, "$H");

    EXPECT_EQ(all[1].name, "Probe Z (Touch Plate)");
    EXPECT_TRUE(all[1].builtIn);
    EXPECT_EQ(all[1].gcode, "G91\nG38.2 Z-50 F100\nG90");

    EXPECT_EQ(all[2].name, "Return to Zero");
    EXPECT_TRUE(all[2].builtIn);
    EXPECT_EQ(all[2].gcode, "G90\nG53 G0 Z0\nG53 G0 X0 Y0");
}

// --- ensureBuiltIns is idempotent ---

TEST_F(MacroManagerTest, EnsureBuiltInsIdempotent) {
    m_mgr->ensureBuiltIns();
    m_mgr->ensureBuiltIns();
    m_mgr->ensureBuiltIns();

    auto all = m_mgr->getAll();
    // Should still be exactly 3, not 6 or 9
    int builtInCount = 0;
    for (const auto& m : all) {
        if (m.builtIn) ++builtInCount;
    }
    EXPECT_EQ(builtInCount, 3);
}

// --- parseLines ---

TEST_F(MacroManagerTest, ParseLinesSplitsCorrectly) {
    Macro m;
    m.gcode = "G90\nG0 X10\nG1 Y20 F500";
    auto lines = m_mgr->parseLines(m);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "G90");
    EXPECT_EQ(lines[1], "G0 X10");
    EXPECT_EQ(lines[2], "G1 Y20 F500");
}

TEST_F(MacroManagerTest, ParseLinesSkipsEmptyAndComments) {
    Macro m;
    m.gcode = "G90\n\n; this is a comment\n  \n(another comment)\nG0 X0";
    auto lines = m_mgr->parseLines(m);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "G90");
    EXPECT_EQ(lines[1], "G0 X0");
}

TEST_F(MacroManagerTest, ParseLinesTrimsWhitespace) {
    Macro m;
    m.gcode = "  G90  \n\tG0 X10\t";
    auto lines = m_mgr->parseLines(m);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "G90");
    EXPECT_EQ(lines[1], "G0 X10");
}

// --- Reorder ---

TEST_F(MacroManagerTest, ReorderUpdatesSortOrder) {
    Macro m1;
    m1.name = "Alpha";
    m1.gcode = "G0";
    m1.sortOrder = 0;
    int id1 = m_mgr->addMacro(m1);

    Macro m2;
    m2.name = "Beta";
    m2.gcode = "G0";
    m2.sortOrder = 1;
    int id2 = m_mgr->addMacro(m2);

    Macro m3;
    m3.name = "Gamma";
    m3.gcode = "G0";
    m3.sortOrder = 2;
    int id3 = m_mgr->addMacro(m3);

    // Reverse the order: Gamma, Beta, Alpha
    m_mgr->reorder({id3, id2, id1});

    auto all = m_mgr->getAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].name, "Gamma");
    EXPECT_EQ(all[1].name, "Beta");
    EXPECT_EQ(all[2].name, "Alpha");
}

// --- getById throws for missing ---

TEST_F(MacroManagerTest, GetByIdThrowsForMissing) {
    EXPECT_THROW(m_mgr->getById(999), std::runtime_error);
}

// --- Built-in macros are editable ---

TEST_F(MacroManagerTest, BuiltInMacrosAreEditable) {
    m_mgr->ensureBuiltIns();
    auto all = m_mgr->getAll();
    ASSERT_GE(all.size(), 1u);

    auto& homing = all[0];
    EXPECT_TRUE(homing.builtIn);

    homing.gcode = "$H\nG0 X0 Y0";
    m_mgr->updateMacro(homing);

    auto updated = m_mgr->getById(homing.id);
    EXPECT_EQ(updated.gcode, "$H\nG0 X0 Y0");
    EXPECT_TRUE(updated.builtIn);
}
