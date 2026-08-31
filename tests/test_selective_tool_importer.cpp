#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "core/database/database.h"
#include "core/database/selective_tool_importer.h"
#include "core/database/supplier_tool_catalog.h"
#include "core/database/tool_database.h"

namespace {

class SelectiveToolImporterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("dw_selective_tools_") + info->name());
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
        ASSERT_TRUE(std::filesystem::create_directories(m_root));
        m_sourcePath = m_root / "supplier.vtdb";
        m_localPath = m_root / "local.vtdb";
        populateSupplier(m_sourcePath);
        m_local = std::make_unique<dw::ToolDatabase>();
        ASSERT_TRUE(m_local->open(m_localPath));
    }

    void TearDown() override {
        m_local.reset();
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    static void populateSupplier(const std::filesystem::path& path) {
        {
            dw::ToolDatabase source;
            ASSERT_TRUE(source.open(path));

            dw::VtdbMaterial material;
            material.id = "supplier-material";
            material.name = "Acrylic";
            ASSERT_TRUE(source.insertMaterial(material));

            dw::VtdbMachine machine;
            machine.id = "supplier-machine";
            machine.name = "Desktop";
            machine.make = "Generic";
            machine.model = "24x24";
            ASSERT_TRUE(source.insertMachine(machine));

            dw::VtdbTreeEntry root;
            root.id = "supplier-root";
            root.name = "Supplier Tools";
            ASSERT_TRUE(source.insertTreeEntry(root));
            dw::VtdbTreeEntry group;
            group.id = "supplier-group";
            group.parent_group_id = root.id;
            group.name = "End Mills";
            ASSERT_TRUE(source.insertTreeEntry(group));

            addTool(source, "tool-a", "leaf-a", "Supplier Tool A", "cut-a", "entity-a", 0.25);
            addTool(source, "tool-b", "leaf-b", "Supplier Tool B", "cut-b", "entity-b", 0.125);
            addTool(source, "tool-c", "leaf-c", "Supplier Tool C", "", "", 0.5);

            ASSERT_TRUE(source.database().execute("PRAGMA journal_mode=DELETE"));
        }
        std::error_code error;
        std::filesystem::remove(path.string() + "-wal", error);
        std::filesystem::remove(path.string() + "-shm", error);
    }

    static void addTool(dw::ToolDatabase& source,
                        const std::string& geometryId,
                        const std::string& leafId,
                        const std::string& name,
                        const std::string& cuttingId,
                        const std::string& entityId,
                        double diameter) {
        dw::VtdbToolGeometry geometry;
        geometry.id = geometryId;
        geometry.name_format = name;
        geometry.diameter = diameter;
        geometry.num_flutes = 2;
        ASSERT_TRUE(source.insertGeometry(geometry));

        dw::VtdbTreeEntry leaf;
        leaf.id = leafId;
        leaf.parent_group_id = "supplier-group";
        leaf.tool_geometry_id = geometryId;
        ASSERT_TRUE(source.insertTreeEntry(leaf));

        if (cuttingId.empty()) return;
        dw::VtdbCuttingData cutting;
        cutting.id = cuttingId;
        cutting.rate_units = 4;
        cutting.length_units = 1;
        cutting.feed_rate = geometryId == "tool-a" ? 120.0 : 80.0;
        cutting.plunge_rate = 30.0;
        cutting.spindle_speed = 18000;
        ASSERT_TRUE(source.insertCuttingData(cutting));

        dw::VtdbToolEntity entity;
        entity.id = entityId;
        entity.material_id = "supplier-material";
        entity.machine_id = "supplier-machine";
        entity.tool_geometry_id = geometryId;
        entity.tool_cutting_data_id = cuttingId;
        ASSERT_TRUE(source.insertEntity(entity));
    }

    static int countRows(dw::ToolDatabase& db, const std::string& table) {
        auto query = db.database().prepare("SELECT COUNT(*) FROM " + table);
        EXPECT_TRUE(query.isValid());
        EXPECT_TRUE(query.step());
        return query.isValid() ? static_cast<int>(query.getInt(0)) : -1;
    }

    dw::SupplierToolCatalog openCatalog() const {
        dw::SupplierToolCatalog catalog;
        EXPECT_TRUE(catalog.open(m_sourcePath));
        return catalog;
    }

    std::filesystem::path m_root;
    std::filesystem::path m_sourcePath;
    std::filesystem::path m_localPath;
    std::unique_ptr<dw::ToolDatabase> m_local;
};

TEST_F(SelectiveToolImporterTest, RejectsDatabaseWithoutVtdbSchema) {
    const auto invalidPath = m_root / "invalid.vtdb";
    {
        dw::Database invalid;
        ASSERT_TRUE(invalid.open(invalidPath));
        ASSERT_TRUE(invalid.execute("CREATE TABLE unrelated (id TEXT)"));
    }
    dw::SupplierToolCatalog catalog;
    const auto opened = catalog.open(invalidPath);
    EXPECT_FALSE(opened);
    EXPECT_EQ(opened.error, dw::SupplierToolCatalogError::InvalidSchema);
    EXPECT_FALSE(catalog.isOpen());
}

TEST_F(SelectiveToolImporterTest, OpensReadOnlySupplierWithoutChangingIt) {
    const auto sizeBefore = std::filesystem::file_size(m_sourcePath);
    const auto timeBefore = std::filesystem::last_write_time(m_sourcePath);
    std::filesystem::permissions(
        m_sourcePath,
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read
            | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);

    {
        auto catalog = openCatalog();
        ASSERT_TRUE(catalog.isOpen());
        EXPECT_EQ(catalog.tools().size(), 3u);
    }
    EXPECT_EQ(std::filesystem::file_size(m_sourcePath), sizeBefore);
    EXPECT_EQ(std::filesystem::last_write_time(m_sourcePath), timeBefore);
    EXPECT_FALSE(std::filesystem::exists(m_sourcePath.string() + "-wal"));
    EXPECT_FALSE(std::filesystem::exists(m_sourcePath.string() + "-shm"));
}

TEST_F(SelectiveToolImporterTest, ReadOnlySupplierPathSupportsUriCharacters) {
#ifdef _WIN32
    // '?' cannot appear in Windows filenames; keep the other URI-hostile chars.
    const auto encodedPath = m_root / "supplier #100%.vtdb";
#else
    const auto encodedPath = m_root / "supplier #100%?.vtdb";
#endif
    ASSERT_TRUE(std::filesystem::copy_file(m_sourcePath, encodedPath));

    dw::SupplierToolCatalog catalog;
    const auto opened = catalog.open(encodedPath);
    ASSERT_TRUE(opened) << opened.message;
    EXPECT_EQ(catalog.tools().size(), 3u);
}

TEST_F(SelectiveToolImporterTest, ListsToolsWithDisplayNamesAndCategoryPaths) {
    auto catalog = openCatalog();
    ASSERT_EQ(catalog.tools().size(), 3u);
    const auto tool = std::find_if(catalog.tools().begin(), catalog.tools().end(),
                                   [](const auto& item) { return item.geometryId == "tool-a"; });
    ASSERT_NE(tool, catalog.tools().end());
    EXPECT_EQ(tool->treeEntryId, "leaf-a");
    EXPECT_EQ(tool->displayName, "Supplier Tool A");
    EXPECT_EQ(tool->categoryPath,
              (std::vector<std::string>{"Supplier Tools", "End Mills"}));
    EXPECT_EQ(tool->cuttingProfileCount, 1u);
}

TEST_F(SelectiveToolImporterTest, CopiesOnlySelectedToolAndItsCompleteGraph) {
    auto catalog = openCatalog();
    const auto copied = dw::SelectiveToolImporter::copySelected(catalog, *m_local, {"tool-a"});
    ASSERT_TRUE(copied) << copied.message;
    EXPECT_EQ(copied.copiedCount, 1u);
    EXPECT_TRUE(m_local->findGeometryById("tool-a"));
    EXPECT_FALSE(m_local->findGeometryById("tool-b"));
    EXPECT_TRUE(m_local->findCuttingDataById("cut-a"));
    EXPECT_FALSE(m_local->findCuttingDataById("cut-b"));
    EXPECT_EQ(m_local->findEntitiesForGeometry("tool-a").size(), 1u);
    EXPECT_EQ(countRows(*m_local, "material"), 1);
    EXPECT_EQ(countRows(*m_local, "machine"), 1);
    EXPECT_EQ(countRows(*m_local, "tool_tree_entry"), 3);
}

TEST_F(SelectiveToolImporterTest, MultiSelectSharesDependenciesAndCategoryAncestors) {
    auto catalog = openCatalog();
    const auto copied = dw::SelectiveToolImporter::copySelected(
        catalog, *m_local, {"tool-a", "tool-b"});
    ASSERT_TRUE(copied) << copied.message;
    EXPECT_EQ(copied.copiedCount, 2u);
    EXPECT_EQ(countRows(*m_local, "tool_geometry"), 2);
    EXPECT_EQ(countRows(*m_local, "tool_cutting_data"), 2);
    EXPECT_EQ(countRows(*m_local, "tool_entity"), 2);
    EXPECT_EQ(countRows(*m_local, "material"), 1);
    EXPECT_EQ(countRows(*m_local, "machine"), 1);
    EXPECT_EQ(countRows(*m_local, "tool_tree_entry"), 4);
}

TEST_F(SelectiveToolImporterTest, RecopyIsIdempotentAndPreservesLocalEdits) {
    auto catalog = openCatalog();
    ASSERT_TRUE(dw::SelectiveToolImporter::copySelected(catalog, *m_local, {"tool-a"}));
    auto edited = m_local->findGeometryById("tool-a");
    ASSERT_TRUE(edited);
    edited->diameter = 0.333;
    edited->name_format = "My edited tool";
    ASSERT_TRUE(m_local->updateGeometry(*edited));

    const auto repeated = dw::SelectiveToolImporter::copySelected(catalog, *m_local, {"tool-a"});
    ASSERT_TRUE(repeated) << repeated.message;
    EXPECT_EQ(repeated.copiedCount, 0u);
    EXPECT_EQ(repeated.alreadyPresentCount, 1u);
    auto preserved = m_local->findGeometryById("tool-a");
    ASSERT_TRUE(preserved);
    EXPECT_DOUBLE_EQ(preserved->diameter, 0.333);
    EXPECT_EQ(preserved->name_format, "My edited tool");
}

TEST_F(SelectiveToolImporterTest, MapsSameNameMaterialAndMachineToLocalIds) {
    dw::VtdbMaterial localMaterial;
    localMaterial.id = "local-material";
    localMaterial.name = "Acrylic";
    ASSERT_TRUE(m_local->insertMaterial(localMaterial));
    dw::VtdbMachine localMachine;
    localMachine.id = "local-machine";
    localMachine.name = "Desktop";
    ASSERT_TRUE(m_local->insertMachine(localMachine));

    auto catalog = openCatalog();
    const auto copied = dw::SelectiveToolImporter::copySelected(catalog, *m_local, {"tool-a"});
    ASSERT_TRUE(copied) << copied.message;
    const auto entities = m_local->findEntitiesForGeometry("tool-a");
    ASSERT_EQ(entities.size(), 1u);
    EXPECT_EQ(entities.front().material_id, "local-material");
    EXPECT_EQ(entities.front().machine_id, "local-machine");
    EXPECT_FALSE(m_local->findMaterialById("supplier-material"));
    EXPECT_FALSE(m_local->findMachineById("supplier-machine"));
}

TEST_F(SelectiveToolImporterTest, DependencyIdentityConflictRollsBackWholeSelection) {
    dw::VtdbCuttingData conflicting;
    conflicting.id = "cut-b";
    conflicting.feed_rate = 999.0;
    ASSERT_TRUE(m_local->insertCuttingData(conflicting));
    auto catalog = openCatalog();
    const auto copied = dw::SelectiveToolImporter::copySelected(
        catalog, *m_local, {"tool-a", "tool-b"});
    EXPECT_FALSE(copied);
    EXPECT_EQ(copied.error, dw::SelectiveToolImportError::IdentityConflict);
    EXPECT_FALSE(m_local->findGeometryById("tool-a"));
    EXPECT_FALSE(m_local->findGeometryById("tool-b"));
    EXPECT_FALSE(m_local->findCuttingDataById("cut-a"));
    EXPECT_EQ(countRows(*m_local, "material"), 0);
    EXPECT_EQ(countRows(*m_local, "tool_tree_entry"), 0);
    EXPECT_TRUE(m_local->findCuttingDataById("cut-b"));
}

TEST_F(SelectiveToolImporterTest, CopiesToolWithoutCuttingEntity) {
    auto catalog = openCatalog();
    const auto copied = dw::SelectiveToolImporter::copySelected(catalog, *m_local, {"tool-c"});
    ASSERT_TRUE(copied) << copied.message;
    EXPECT_TRUE(m_local->findGeometryById("tool-c"));
    EXPECT_TRUE(m_local->findEntitiesForGeometry("tool-c").empty());
    EXPECT_EQ(countRows(*m_local, "tool_cutting_data"), 0);
    EXPECT_EQ(countRows(*m_local, "material"), 0);
    EXPECT_EQ(countRows(*m_local, "machine"), 0);
    EXPECT_EQ(countRows(*m_local, "tool_tree_entry"), 3);
}

TEST_F(SelectiveToolImporterTest, RejectsUnknownSelectionWithoutWrites) {
    auto catalog = openCatalog();
    const auto copied = dw::SelectiveToolImporter::copySelected(catalog, *m_local, {"missing"});
    EXPECT_FALSE(copied);
    EXPECT_EQ(copied.error, dw::SelectiveToolImportError::UnknownTool);
    EXPECT_EQ(countRows(*m_local, "tool_geometry"), 0);
}

TEST_F(SelectiveToolImporterTest, LegacyWholeDatabaseImportUsesSelectiveEngine) {
    EXPECT_EQ(m_local->importFromVtdb(m_sourcePath), 3);
    EXPECT_EQ(m_local->importFromVtdb(m_sourcePath), 0);
    EXPECT_EQ(countRows(*m_local, "tool_geometry"), 3);
    EXPECT_EQ(countRows(*m_local, "tool_tree_entry"), 5);
}

TEST(SupplierToolCatalogIntegration, OpensConfiguredSupplierDatabaseReadOnly) {
    const char* configuredPath = std::getenv("DW_TEST_SUPPLIER_VTDB");
    if (!configuredPath || configuredPath[0] == '\0')
        GTEST_SKIP() << "Set DW_TEST_SUPPLIER_VTDB to exercise a real supplier database";

    const std::filesystem::path path(configuredPath);
    ASSERT_TRUE(std::filesystem::is_regular_file(path));
    const auto sizeBefore = std::filesystem::file_size(path);
    const auto timeBefore = std::filesystem::last_write_time(path);
    const auto optionalFileState = [](const std::filesystem::path& optionalPath) {
        using FileState = std::pair<std::uintmax_t, std::filesystem::file_time_type>;
        return std::filesystem::exists(optionalPath)
            ? std::optional<FileState>({std::filesystem::file_size(optionalPath),
                                        std::filesystem::last_write_time(optionalPath)})
            : std::optional<FileState>();
    };
    const auto walPath = std::filesystem::path(path.string() + "-wal");
    const auto shmPath = std::filesystem::path(path.string() + "-shm");
    const auto walBefore = optionalFileState(walPath);
    const auto shmBefore = optionalFileState(shmPath);

    dw::SupplierToolCatalog catalog;
    const auto opened = catalog.open(path);
    ASSERT_TRUE(opened) << opened.message;
    EXPECT_FALSE(catalog.tools().empty());

    const auto scratch = std::filesystem::temp_directory_path()
        / ("dw_supplier_copy_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directories(scratch));
    {
        dw::ToolDatabase destination;
        ASSERT_TRUE(destination.open(scratch / "local.vtdb"));
        const auto copied = dw::SelectiveToolImporter::copySelected(
            catalog, destination, {catalog.tools().front().geometryId});
        ASSERT_TRUE(copied) << copied.message;
        EXPECT_EQ(copied.copiedCount, 1u);
        EXPECT_TRUE(destination.findGeometryById(catalog.tools().front().geometryId));
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(scratch, cleanupError);
    EXPECT_FALSE(cleanupError);

    catalog.close();
    EXPECT_EQ(std::filesystem::file_size(path), sizeBefore);
    EXPECT_EQ(std::filesystem::last_write_time(path), timeBefore);
    EXPECT_EQ(optionalFileState(walPath), walBefore);
    EXPECT_EQ(optionalFileState(shmPath), shmBefore);
}

} // namespace
