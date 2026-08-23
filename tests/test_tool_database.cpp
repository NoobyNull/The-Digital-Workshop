// Digital Workshop - Tool Database Tests

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include "core/database/tool_database.h"

namespace {

// Fixture: provides an in-memory ToolDatabase per test
class ToolDatabaseTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_toolDb.open(":memory:"));
    }

    dw::ToolDatabase m_toolDb;
};

class LegacyToolDatabaseFile {
  public:
    LegacyToolDatabaseFile() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("dw_tool_database_migration_" + std::to_string(nonce));
        std::filesystem::create_directories(root);
        path = root / "legacy.vtdb";
    }

    ~LegacyToolDatabaseFile() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::filesystem::path path;
};

}  // namespace

// --- Schema ---

TEST_F(ToolDatabaseTest, Schema_TablesCreated) {
    auto& db = m_toolDb.database();

    // Check version table
    auto stmt1 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='version'");
    EXPECT_TRUE(stmt1.step());

    // Check material table
    auto stmt2 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='material'");
    EXPECT_TRUE(stmt2.step());

    // Check machine table
    auto stmt3 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='machine'");
    EXPECT_TRUE(stmt3.step());

    // Check tool_geometry table
    auto stmt4 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='tool_geometry'");
    EXPECT_TRUE(stmt4.step());

    // Check tool_cutting_data table
    auto stmt5 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='tool_cutting_data'");
    EXPECT_TRUE(stmt5.step());

    // Check tool_entity table
    auto stmt6 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='tool_entity'");
    EXPECT_TRUE(stmt6.step());

    // Check tool_tree_entry table
    auto stmt7 = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='tool_tree_entry'");
    EXPECT_TRUE(stmt7.step());
}

TEST_F(ToolDatabaseTest, Schema_VersionSet) {
    auto& db = m_toolDb.database();
    auto stmt = db.prepare("SELECT version FROM version");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.getInt(0), 1);
}

TEST(ToolDatabaseSchemaMigration, ExistingVectricSchemaGetsExtendedColumnsOnce) {
    LegacyToolDatabaseFile file;
    {
        dw::Database legacy;
        ASSERT_TRUE(legacy.open(file.path.string()));
        ASSERT_TRUE(legacy.execute(
            "CREATE TABLE version (version INTEGER NOT NULL UNIQUE PRIMARY KEY)"));
        ASSERT_TRUE(legacy.execute("INSERT INTO version(version) VALUES (1)"));
        ASSERT_TRUE(legacy.execute(R"(
            CREATE TABLE machine (
                id TEXT NOT NULL UNIQUE PRIMARY KEY,
                name TEXT NOT NULL UNIQUE,
                make TEXT,
                model TEXT,
                controller_type TEXT,
                dimensions_units INTEGER,
                max_width REAL,
                max_height REAL,
                support_rotary INTEGER,
                support_tool_change INTEGER,
                has_laser_head INTEGER
            )
        )"));
    }

    for (int pass = 0; pass < 2; ++pass) {
        dw::ToolDatabase migrated;
        ASSERT_TRUE(migrated.open(file.path));
        auto columns = migrated.database().prepare(
            "SELECT COUNT(*) FROM pragma_table_info('machine') "
            "WHERE name IN ('spindle_power_watts', 'max_rpm', 'drive_type')");
        ASSERT_TRUE(columns.step());
        EXPECT_EQ(columns.getInt(0), 3) << "migration pass " << pass;
    }
}

// --- Basic operations ---

TEST_F(ToolDatabaseTest, CanOpenDatabase) {
    dw::ToolDatabase db;
    EXPECT_TRUE(db.open(":memory:"));
}

// --- CRUD Operations ---

TEST_F(ToolDatabaseTest, Material_InsertAndFind) {
    dw::VtdbMaterial mat;
    mat.id = "test-mat-001";
    mat.name = "Aluminum";

    EXPECT_TRUE(m_toolDb.insertMaterial(mat));

    auto found = m_toolDb.findMaterialById("test-mat-001");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->name, "Aluminum");
}

TEST_F(ToolDatabaseTest, Machine_InsertAndFind) {
    dw::VtdbMachine mach;
    mach.id = "test-mach-001";
    mach.name = "Shapeoko";
    mach.make = "Carbide3D";
    mach.model = "Shapeoko 5";

    EXPECT_TRUE(m_toolDb.insertMachine(mach));

    auto found = m_toolDb.findMachineById("test-mach-001");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->name, "Shapeoko");
}

TEST_F(ToolDatabaseTest, ToolGeometry_InsertAndFind) {
    dw::VtdbToolGeometry geom;
    geom.id = "test-geom-001";
    geom.name_format = "EndMill_1/4in";
    geom.tool_type = dw::VtdbToolType::EndMill;
    geom.units = dw::VtdbUnits::Imperial;
    geom.diameter = 0.25;
    geom.num_flutes = 2;

    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    auto found = m_toolDb.findGeometryById("test-geom-001");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->diameter, 0.25);
}

// --- Material findByName ---

TEST_F(ToolDatabaseTest, Material_FindByName) {
    dw::VtdbMaterial mat;
    mat.id = "mat-byname";
    mat.name = "Hardwood";
    EXPECT_TRUE(m_toolDb.insertMaterial(mat));

    auto found = m_toolDb.findMaterialByName("Hardwood");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->id, "mat-byname");

    EXPECT_FALSE(m_toolDb.findMaterialByName("NonExistent"));
}

TEST_F(ToolDatabaseTest, Material_FindAll) {
    dw::VtdbMaterial m1; m1.id = "m1"; m1.name = "Birch";
    dw::VtdbMaterial m2; m2.id = "m2"; m2.name = "Aluminum";
    EXPECT_TRUE(m_toolDb.insertMaterial(m1));
    EXPECT_TRUE(m_toolDb.insertMaterial(m2));

    auto all = m_toolDb.findAllMaterials();
    EXPECT_EQ(all.size(), 2);
    // Ordered by name
    EXPECT_EQ(all[0].name, "Aluminum");
    EXPECT_EQ(all[1].name, "Birch");
}

// --- Machine CRUD ---

TEST_F(ToolDatabaseTest, Machine_AllFields) {
    dw::VtdbMachine mach;
    mach.id = "mach-full";
    mach.name = "CNC Router";
    mach.make = "Avid";
    mach.model = "PRO 4896";
    mach.controller_type = "Mach4";
    mach.dimensions_units = 1;
    mach.max_width = 96.0;
    mach.max_height = 48.0;
    mach.support_rotary = 0;
    mach.support_tool_change = 1;
    mach.has_laser_head = 0;

    EXPECT_TRUE(m_toolDb.insertMachine(mach));

    auto found = m_toolDb.findMachineById("mach-full");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->make, "Avid");
    EXPECT_EQ(found->model, "PRO 4896");
    EXPECT_EQ(found->controller_type, "Mach4");
    EXPECT_EQ(found->max_width, 96.0);
    EXPECT_EQ(found->max_height, 48.0);
    EXPECT_EQ(found->support_tool_change, 1);
}

TEST_F(ToolDatabaseTest, Machine_FindAll) {
    dw::VtdbMachine m1; m1.id = "m1"; m1.name = "Alpha";
    dw::VtdbMachine m2; m2.id = "m2"; m2.name = "Beta";
    EXPECT_TRUE(m_toolDb.insertMachine(m1));
    EXPECT_TRUE(m_toolDb.insertMachine(m2));

    auto all = m_toolDb.findAllMachines();
    EXPECT_EQ(all.size(), 2);
}

// --- Geometry CRUD ---

TEST_F(ToolDatabaseTest, Geometry_UpdateAndRemove) {
    dw::VtdbToolGeometry geom;
    geom.id = "geom-upd";
    geom.name_format = "VBit_60deg";
    geom.tool_type = dw::VtdbToolType::VBit;
    geom.units = dw::VtdbUnits::Imperial;
    geom.diameter = 0.5;
    geom.included_angle = 60.0;
    geom.num_flutes = 2;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    // Update
    geom.diameter = 0.75;
    geom.included_angle = 90.0;
    EXPECT_TRUE(m_toolDb.updateGeometry(geom));

    auto found = m_toolDb.findGeometryById("geom-upd");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->diameter, 0.75);
    EXPECT_EQ(found->included_angle, 90.0);

    // Remove
    EXPECT_TRUE(m_toolDb.removeGeometry("geom-upd"));
    EXPECT_FALSE(m_toolDb.findGeometryById("geom-upd"));
}

TEST_F(ToolDatabaseTest, Geometry_FindAll) {
    dw::VtdbToolGeometry g1;
    g1.id = "ga"; g1.name_format = "EndMill_A"; g1.tool_type = dw::VtdbToolType::EndMill;
    dw::VtdbToolGeometry g2;
    g2.id = "gb"; g2.name_format = "BallNose_B"; g2.tool_type = dw::VtdbToolType::BallNose;
    EXPECT_TRUE(m_toolDb.insertGeometry(g1));
    EXPECT_TRUE(m_toolDb.insertGeometry(g2));

    auto all = m_toolDb.findAllGeometries();
    EXPECT_EQ(all.size(), 2);
}

// --- Cutting Data CRUD ---

TEST_F(ToolDatabaseTest, CuttingData_InsertAndFind) {
    dw::VtdbCuttingData cd;
    cd.id = "cd-001";
    cd.rate_units = 4;
    cd.feed_rate = 100.0;
    cd.plunge_rate = 50.0;
    cd.spindle_speed = 18000;
    cd.spindle_dir = 0;
    cd.stepdown = 0.1;
    cd.stepover = 0.4;
    cd.tool_number = 1;

    EXPECT_TRUE(m_toolDb.insertCuttingData(cd));

    auto found = m_toolDb.findCuttingDataById("cd-001");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->feed_rate, 100.0);
    EXPECT_EQ(found->plunge_rate, 50.0);
    EXPECT_EQ(found->spindle_speed, 18000);
    EXPECT_EQ(found->stepdown, 0.1);
    EXPECT_EQ(found->stepover, 0.4);
    EXPECT_EQ(found->tool_number, 1);
}

TEST_F(ToolDatabaseTest, CuttingData_UpdateAndRemove) {
    dw::VtdbCuttingData cd;
    cd.id = "cd-upd";
    cd.rate_units = 4;
    cd.feed_rate = 80.0;
    cd.spindle_speed = 12000;
    EXPECT_TRUE(m_toolDb.insertCuttingData(cd));

    cd.feed_rate = 120.0;
    cd.spindle_speed = 24000;
    EXPECT_TRUE(m_toolDb.updateCuttingData(cd));

    auto found = m_toolDb.findCuttingDataById("cd-upd");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->feed_rate, 120.0);
    EXPECT_EQ(found->spindle_speed, 24000);

    EXPECT_TRUE(m_toolDb.removeCuttingData("cd-upd"));
    EXPECT_FALSE(m_toolDb.findCuttingDataById("cd-upd"));
}

// --- Tree Entry ---

TEST_F(ToolDatabaseTest, TreeEntry_InsertAndFind) {
    dw::VtdbToolGeometry geom;
    geom.id = "test-geom-tree";
    geom.name_format = "BallNose_1/8in";
    geom.tool_type = dw::VtdbToolType::BallNose;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    dw::VtdbTreeEntry entry;
    entry.id = "test-tree-001";
    entry.parent_group_id = "";  // root entry
    entry.sibling_order = 0;
    entry.tool_geometry_id = "test-geom-tree";
    entry.name = "Ball Nose 1/8\"";
    entry.notes = "For detail work";
    entry.expanded = 0;

    EXPECT_TRUE(m_toolDb.insertTreeEntry(entry));

    auto roots = m_toolDb.findRootEntries();
    EXPECT_EQ(roots.size(), 1);
    EXPECT_EQ(roots[0].name, "Ball Nose 1/8\"");
}

TEST_F(ToolDatabaseTest, TreeEntry_NestedGroupWithChildren) {
    // Create a group (no geometry)
    dw::VtdbTreeEntry group;
    group.id = "group-001";
    group.parent_group_id = "";
    group.sibling_order = 0;
    group.name = "End Mills";
    group.expanded = 1;
    EXPECT_TRUE(m_toolDb.insertTreeEntry(group));

    // Create geometry for child
    dw::VtdbToolGeometry geom;
    geom.id = "geom-child";
    geom.name_format = "EndMill_1/4";
    geom.tool_type = dw::VtdbToolType::EndMill;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    // Create child entry under the group
    dw::VtdbTreeEntry child;
    child.id = "child-001";
    child.parent_group_id = "group-001";
    child.sibling_order = 0;
    child.tool_geometry_id = "geom-child";
    child.name = "1/4\" End Mill";
    EXPECT_TRUE(m_toolDb.insertTreeEntry(child));

    auto children = m_toolDb.findChildrenOf("group-001");
    EXPECT_EQ(children.size(), 1);
    EXPECT_EQ(children[0].tool_geometry_id, "geom-child");

    auto all = m_toolDb.getAllTreeEntries();
    EXPECT_EQ(all.size(), 2);
}

TEST_F(ToolDatabaseTest, TreeEntry_UpdateAndRemove) {
    dw::VtdbTreeEntry entry;
    entry.id = "tree-upd";
    entry.parent_group_id = "";
    entry.sibling_order = 0;
    entry.name = "Original";
    EXPECT_TRUE(m_toolDb.insertTreeEntry(entry));

    entry.name = "Updated";
    entry.expanded = 1;
    EXPECT_TRUE(m_toolDb.updateTreeEntry(entry));

    auto roots = m_toolDb.findRootEntries();
    ASSERT_EQ(roots.size(), 1);
    EXPECT_EQ(roots[0].name, "Updated");
    EXPECT_EQ(roots[0].expanded, 1);

    EXPECT_TRUE(m_toolDb.removeTreeEntry("tree-upd"));
    EXPECT_EQ(m_toolDb.findRootEntries().size(), 0);
}

// --- Tool Entity (junction) ---

TEST_F(ToolDatabaseTest, Entity_InsertAndQuery) {
    // Setup: material, machine, geometry, cutting data
    dw::VtdbMaterial mat; mat.id = "mat-ent"; mat.name = "MDF";
    EXPECT_TRUE(m_toolDb.insertMaterial(mat));

    dw::VtdbMachine mach; mach.id = "mach-ent"; mach.name = "Router1";
    EXPECT_TRUE(m_toolDb.insertMachine(mach));

    dw::VtdbToolGeometry geom;
    geom.id = "geom-ent"; geom.name_format = "EM"; geom.tool_type = dw::VtdbToolType::EndMill;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    dw::VtdbCuttingData cd;
    cd.id = "cd-ent"; cd.rate_units = 4; cd.feed_rate = 60.0;
    EXPECT_TRUE(m_toolDb.insertCuttingData(cd));

    // Insert entity with specific material
    dw::VtdbToolEntity ent;
    ent.id = "ent-001";
    ent.material_id = "mat-ent";
    ent.machine_id = "mach-ent";
    ent.tool_geometry_id = "geom-ent";
    ent.tool_cutting_data_id = "cd-ent";
    EXPECT_TRUE(m_toolDb.insertEntity(ent));

    auto forGeom = m_toolDb.findEntitiesForGeometry("geom-ent");
    EXPECT_EQ(forGeom.size(), 1);
    EXPECT_EQ(forGeom[0].material_id, "mat-ent");

    auto forMat = m_toolDb.findEntitiesForMaterial("mat-ent");
    EXPECT_EQ(forMat.size(), 1);

    EXPECT_TRUE(m_toolDb.removeEntity("ent-001"));
    EXPECT_EQ(m_toolDb.findEntitiesForGeometry("geom-ent").size(), 0);
}

TEST_F(ToolDatabaseTest, Entity_NullMaterialMeansAllMaterials) {
    dw::VtdbMachine mach; mach.id = "mach-null"; mach.name = "Router2";
    EXPECT_TRUE(m_toolDb.insertMachine(mach));

    dw::VtdbToolGeometry geom;
    geom.id = "geom-null"; geom.name_format = "EM_null"; geom.tool_type = dw::VtdbToolType::EndMill;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    dw::VtdbCuttingData cd;
    cd.id = "cd-null"; cd.rate_units = 4;
    EXPECT_TRUE(m_toolDb.insertCuttingData(cd));

    // Empty material_id -> NULL in DB
    dw::VtdbToolEntity ent;
    ent.id = "ent-null";
    ent.material_id = "";
    ent.machine_id = "mach-null";
    ent.tool_geometry_id = "geom-null";
    ent.tool_cutting_data_id = "cd-null";
    EXPECT_TRUE(m_toolDb.insertEntity(ent));

    auto forGeom = m_toolDb.findEntitiesForGeometry("geom-null");
    ASSERT_EQ(forGeom.size(), 1);
    EXPECT_TRUE(forGeom[0].material_id.empty());
}

// --- getToolView ---

TEST_F(ToolDatabaseTest, GetToolView_AssemblesAllParts) {
    // Material
    dw::VtdbMaterial mat; mat.id = "view-mat"; mat.name = "Plywood";
    EXPECT_TRUE(m_toolDb.insertMaterial(mat));

    // Machine
    dw::VtdbMachine mach; mach.id = "view-mach"; mach.name = "Shapeoko";
    EXPECT_TRUE(m_toolDb.insertMachine(mach));

    // Geometry
    dw::VtdbToolGeometry geom;
    geom.id = "view-geom";
    geom.name_format = "EndMill_1/4";
    geom.tool_type = dw::VtdbToolType::EndMill;
    geom.diameter = 0.25;
    geom.num_flutes = 2;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    // Cutting data
    dw::VtdbCuttingData cd;
    cd.id = "view-cd";
    cd.rate_units = 4;
    cd.feed_rate = 100.0;
    cd.spindle_speed = 18000;
    cd.stepdown = 0.1;
    EXPECT_TRUE(m_toolDb.insertCuttingData(cd));

    // Entity linking them
    dw::VtdbToolEntity ent;
    ent.id = "view-ent";
    ent.material_id = "view-mat";
    ent.machine_id = "view-mach";
    ent.tool_geometry_id = "view-geom";
    ent.tool_cutting_data_id = "view-cd";
    EXPECT_TRUE(m_toolDb.insertEntity(ent));

    // Get assembled view
    auto view = m_toolDb.getToolView("view-geom", "view-mat", "view-mach");
    ASSERT_TRUE(view);
    EXPECT_EQ(view->geometry.diameter, 0.25);
    EXPECT_EQ(view->cutting_data.feed_rate, 100.0);
    EXPECT_EQ(view->cutting_data.spindle_speed, 18000);
    EXPECT_EQ(view->material.name, "Plywood");
    EXPECT_EQ(view->machine.name, "Shapeoko");
}

TEST_F(ToolDatabaseTest, GetToolView_PrefersSpecificMaterial) {
    dw::VtdbMaterial mat; mat.id = "pref-mat"; mat.name = "Oak";
    EXPECT_TRUE(m_toolDb.insertMaterial(mat));

    dw::VtdbMachine mach; mach.id = "pref-mach"; mach.name = "Router";
    EXPECT_TRUE(m_toolDb.insertMachine(mach));

    dw::VtdbToolGeometry geom;
    geom.id = "pref-geom"; geom.name_format = "EM"; geom.tool_type = dw::VtdbToolType::EndMill;
    EXPECT_TRUE(m_toolDb.insertGeometry(geom));

    // Generic cutting data (null material)
    dw::VtdbCuttingData cdGeneric;
    cdGeneric.id = "cd-generic"; cdGeneric.rate_units = 4; cdGeneric.feed_rate = 50.0;
    EXPECT_TRUE(m_toolDb.insertCuttingData(cdGeneric));

    dw::VtdbToolEntity entGeneric;
    entGeneric.id = "ent-generic";
    entGeneric.material_id = "";
    entGeneric.machine_id = "pref-mach";
    entGeneric.tool_geometry_id = "pref-geom";
    entGeneric.tool_cutting_data_id = "cd-generic";
    EXPECT_TRUE(m_toolDb.insertEntity(entGeneric));

    // Specific cutting data for Oak
    dw::VtdbCuttingData cdSpecific;
    cdSpecific.id = "cd-specific"; cdSpecific.rate_units = 4; cdSpecific.feed_rate = 80.0;
    EXPECT_TRUE(m_toolDb.insertCuttingData(cdSpecific));

    dw::VtdbToolEntity entSpecific;
    entSpecific.id = "ent-specific";
    entSpecific.material_id = "pref-mat";
    entSpecific.machine_id = "pref-mach";
    entSpecific.tool_geometry_id = "pref-geom";
    entSpecific.tool_cutting_data_id = "cd-specific";
    EXPECT_TRUE(m_toolDb.insertEntity(entSpecific));

    // Should prefer specific material over NULL
    auto view = m_toolDb.getToolView("pref-geom", "pref-mat", "pref-mach");
    ASSERT_TRUE(view);
    EXPECT_EQ(view->cutting_data.feed_rate, 80.0);
}

TEST_F(ToolDatabaseTest, GetToolView_NotFound) {
    EXPECT_FALSE(m_toolDb.getToolView("nonexistent", "none", "none"));
}

// --- Import Tests ---

TEST_F(ToolDatabaseTest, Import_FromVectricDatabase) {
    const char* vectricPath = "/home/matthew/Downloads/IDC Woodcraft Vectric Tool Database Library Update rev 25-12.vtdb";

    dw::ToolDatabase importDb;
    ASSERT_TRUE(importDb.open(":memory:"));

    int imported = importDb.importFromVtdb(vectricPath);

    if (imported > 0) {
        auto materials = importDb.findAllMaterials();
        auto machines = importDb.findAllMachines();
        auto geometries = importDb.findAllGeometries();

        EXPECT_GT(materials.size(), 0u);
        EXPECT_GT(machines.size(), 0u);
        EXPECT_GT(geometries.size(), 0u);
        EXPECT_EQ(imported, static_cast<int>(geometries.size()));
    }
}

TEST_F(ToolDatabaseTest, Import_NonExistentFile) {
    int result = m_toolDb.importFromVtdb("/nonexistent/path.vtdb");
    EXPECT_EQ(result, -1);
}
