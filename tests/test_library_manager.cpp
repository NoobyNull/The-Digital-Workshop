// Digital Workshop - Library Manager Tests

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/schema.h"
#include "core/library/library_manager.h"
#include "core/utils/file_utils.h"

#include <cstring>
#include <filesystem>

namespace {

class LibraryManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
        m_mgr = std::make_unique<dw::LibraryManager>(m_db);

        m_tmpDir = std::filesystem::temp_directory_path() / "dw_test_libmgr";
        std::filesystem::create_directories(m_tmpDir);
    }

    void TearDown() override { std::filesystem::remove_all(m_tmpDir); }

    // Write a minimal binary STL (1 triangle) to disk
    dw::Path writeMiniSTL(const std::string& name) {
        auto path = m_tmpDir / (name + ".stl");

        dw::ByteBuffer buf(80 + 4 + 50, 0);
        dw::u32 triCount = 1;
        std::memcpy(buf.data() + 80, &triCount, sizeof(triCount));

        // Write one triangle with non-degenerate vertices
        float tri[12] = {
            0,
            0,
            1, // normal
            0,
            0,
            0, // v0
            1,
            0,
            0, // v1
            0,
            1,
            0 // v2
        };
        std::memcpy(buf.data() + 84, tri, sizeof(tri));

        EXPECT_TRUE(dw::file::writeBinary(path, buf));
        return path;
    }

    // Write a different STL (different content → different hash)
    dw::Path writeDifferentSTL(const std::string& name) {
        auto path = m_tmpDir / (name + ".stl");

        dw::ByteBuffer buf(80 + 4 + 50, 0);
        dw::u32 triCount = 1;
        std::memcpy(buf.data() + 80, &triCount, sizeof(triCount));

        float tri[12] = {0,
                         0,
                         1,
                         0,
                         0,
                         0,
                         2,
                         0,
                         0, // different vertex → different hash
                         0,
                         2,
                         0};
        std::memcpy(buf.data() + 84, tri, sizeof(tri));

        EXPECT_TRUE(dw::file::writeBinary(path, buf));
        return path;
    }

    dw::Database m_db;
    std::unique_ptr<dw::LibraryManager> m_mgr;
    std::filesystem::path m_tmpDir;
};

} // namespace

// Minimal valid 1x1 white PNG.
constexpr dw::u8 kOnePixelPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
    0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
    0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// --- Import ---

TEST_F(LibraryManagerTest, ImportModel_Success) {
    auto path = writeMiniSTL("cube");
    auto result = m_mgr->importModel(path);
    EXPECT_TRUE(result.success) << "Error: " << result.error;
    EXPECT_GT(result.modelId, 0);
    EXPECT_FALSE(result.isDuplicate);
}

TEST_F(LibraryManagerTest, ImportModel_UsesSidecarThumbnailWhenPresent) {
    auto path = writeMiniSTL("sidecar_import");
    const auto imagePath = m_tmpDir / "sidecar_import.png";
    ASSERT_TRUE(dw::file::writeBinary(imagePath, kOnePixelPng, sizeof(kOnePixelPng)));

    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << "Error: " << result.error;

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->thumbnailPath.extension(), ".tga");
    EXPECT_TRUE(dw::file::exists(model->thumbnailPath));

    EXPECT_TRUE(m_mgr->removeModel(result.modelId));
}

TEST_F(LibraryManagerTest, ImportModel_DuplicateDetected) {
    auto path = writeMiniSTL("cube");
    auto r1 = m_mgr->importModel(path);
    ASSERT_TRUE(r1.success) << r1.error;

    // Import same file again — should detect duplicate
    auto r2 = m_mgr->importModel(path);
    EXPECT_TRUE(r2.isDuplicate);
}

TEST_F(LibraryManagerTest, ImportModel_NonExistent) {
    auto result = m_mgr->importModel("/nonexistent/model.stl");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(LibraryManagerTest, ImportModel_UnsupportedFormat) {
    auto path = m_tmpDir / "model.fbx";
    ASSERT_TRUE(dw::file::writeText(path, "not a real fbx"));
    auto result = m_mgr->importModel(path);
    EXPECT_FALSE(result.success);
}

// --- Query ---

TEST_F(LibraryManagerTest, GetAllModels_Empty) {
    auto models = m_mgr->getAllModels();
    EXPECT_TRUE(models.empty());
}

TEST_F(LibraryManagerTest, GetAllModels_AfterImport) {
    writeMiniSTL("a");
    writeDifferentSTL("b");
    m_mgr->importModel(m_tmpDir / "a.stl");
    m_mgr->importModel(m_tmpDir / "b.stl");

    auto models = m_mgr->getAllModels();
    EXPECT_EQ(models.size(), 2u);
}

TEST_F(LibraryManagerTest, UpdateTagStatus_PersistsStatus) {
    auto path = writeMiniSTL("taggable");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    ASSERT_TRUE(m_mgr->updateTagStatus(result.modelId, 2));

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 2);
}

TEST_F(LibraryManagerTest, SearchModels) {
    auto pathA = writeMiniSTL("widget_bracket");
    m_mgr->importModel(pathA);

    auto pathB = writeDifferentSTL("gear_shaft");
    m_mgr->importModel(pathB);

    auto results = m_mgr->searchModels("widget");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "widget_bracket");
}

TEST_F(LibraryManagerTest, GetModel_ById) {
    auto path = writeMiniSTL("mymodel");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->name, "mymodel");
    EXPECT_EQ(model->fileFormat, "stl");
    EXPECT_GT(model->vertexCount, 0u);
}

// --- Update ---

TEST_F(LibraryManagerTest, UpdateTags) {
    auto path = writeMiniSTL("tagged");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);

    EXPECT_TRUE(m_mgr->updateTags(result.modelId, {"cnc", "bracket"}));

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tags.size(), 2u);
}

TEST_F(LibraryManagerTest, SetThumbnailFromImage_ConvertsImageToTgaAndUpdatesRecord) {
    auto path = writeMiniSTL("with_sidecar");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    const auto imagePath = m_tmpDir / "with_sidecar.png";
    ASSERT_TRUE(dw::file::writeBinary(imagePath, kOnePixelPng, sizeof(kOnePixelPng)));

    ASSERT_TRUE(m_mgr->setThumbnailFromImage(result.modelId, imagePath));

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->thumbnailPath.extension(), ".tga");
    ASSERT_TRUE(dw::file::exists(model->thumbnailPath));

    auto thumbnail = dw::file::readBinary(model->thumbnailPath);
    ASSERT_TRUE(thumbnail.has_value());
    ASSERT_GE(thumbnail->size(), 18u);
    EXPECT_EQ((*thumbnail)[2], 2);
    EXPECT_EQ((*thumbnail)[12] | ((*thumbnail)[13] << 8), 512);
    EXPECT_EQ((*thumbnail)[14] | ((*thumbnail)[15] << 8), 512);
    EXPECT_EQ((*thumbnail)[16], 32);
}

// --- Remove ---

TEST_F(LibraryManagerTest, RemoveModel) {
    auto path = writeMiniSTL("removable");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(m_mgr->modelCount(), 1);

    EXPECT_TRUE(m_mgr->removeModel(result.modelId));
    EXPECT_EQ(m_mgr->modelCount(), 0);
}

// --- ModelExists ---

TEST_F(LibraryManagerTest, ModelExists) {
    auto path = writeMiniSTL("exists_test");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success);

    auto model = m_mgr->getModel(result.modelId);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(m_mgr->modelExists(model->hash));
    EXPECT_FALSE(m_mgr->modelExists("nonexistent_hash"));
}

// --- ModelCount ---

TEST_F(LibraryManagerTest, ModelCount) {
    EXPECT_EQ(m_mgr->modelCount(), 0);
    m_mgr->importModel(writeMiniSTL("a"));
    EXPECT_EQ(m_mgr->modelCount(), 1);
    m_mgr->importModel(writeDifferentSTL("b"));
    EXPECT_EQ(m_mgr->modelCount(), 2);
}

TEST_F(LibraryManagerTest, MaintenanceRemovesNestedEmptyCategoriesOnly) {
    auto path = writeMiniSTL("memorial");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto usedRoot = m_mgr->createCategory("Military");
    ASSERT_TRUE(usedRoot.has_value());
    auto usedChild = m_mgr->createCategory("Marines", usedRoot);
    ASSERT_TRUE(usedChild.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *usedChild));

    auto emptyRoot = m_mgr->createCategory("CNC Art");
    ASSERT_TRUE(emptyRoot.has_value());
    auto emptyChild = m_mgr->createCategory("Empty Leaf", emptyRoot);
    ASSERT_TRUE(emptyChild.has_value());

    auto report = m_mgr->runMaintenance();

    EXPECT_GE(report.categoriesRemoved, 2);
    EXPECT_TRUE(m_mgr->filterByCategory(*usedRoot).size() == 1);
    auto categories = m_mgr->getAllCategories();
    EXPECT_EQ(std::count_if(categories.begin(), categories.end(), [](const dw::CategoryRecord& c) {
                  return c.name == "CNC Art" || c.name == "Empty Leaf";
              }),
              0);
}

TEST_F(LibraryManagerTest, MaintenancePromotesChildrenOutOfWorkflowCategories) {
    auto path = writeMiniSTL("badge");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto workflow = m_mgr->createCategory("3D Print");
    ASSERT_TRUE(workflow.has_value());
    auto badge = m_mgr->createCategory("Badge", workflow);
    ASSERT_TRUE(badge.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *badge));

    auto report = m_mgr->runMaintenance();

    EXPECT_GE(report.categoriesRemoved, 1);
    auto categories = m_mgr->getAllCategories();
    EXPECT_EQ(std::count_if(categories.begin(), categories.end(), [](const dw::CategoryRecord& c) {
                  return c.name == "3D Print";
              }),
              0);

    auto symbols = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Symbols" && !c.parentId.has_value();
    });
    ASSERT_NE(symbols, categories.end());
    auto promoted = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Badges" && c.parentId == symbols->id;
    });
    ASSERT_NE(promoted, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(promoted->id).size(), 1u);
}

TEST_F(LibraryManagerTest, MaintenanceMapsCncDecorativeToDecor) {
    auto path = writeMiniSTL("frame");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto workflow = m_mgr->createCategory("CNC Decorative");
    ASSERT_TRUE(workflow.has_value());
    auto frame = m_mgr->createCategory("Frame", workflow);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *frame));

    auto report = m_mgr->runMaintenance();

    EXPECT_GE(report.categoriesRemoved, 1);
    auto categories = m_mgr->getAllCategories();
    auto decor = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Decor" && !c.parentId.has_value();
    });
    ASSERT_NE(decor, categories.end());
    auto decorFrame = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Frame" && c.parentId == decor->id;
    });
    ASSERT_NE(decorFrame, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(decorFrame->id).size(), 1u);
}

TEST_F(LibraryManagerTest, MaintenanceRemovesGenericCncLeafAssignments) {
    auto path = writeMiniSTL("generic");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto workflow = m_mgr->createCategory("CNC Files");
    ASSERT_TRUE(workflow.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *workflow));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    EXPECT_EQ(std::count_if(categories.begin(), categories.end(), [](const dw::CategoryRecord& c) {
                  return c.name == "CNC Files";
              }),
              0);
}

TEST_F(LibraryManagerTest, MaintenanceStripsWorkflowPrefixFromMeaningfulCategory) {
    auto path = writeMiniSTL("panel");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto decorative = m_mgr->createCategory("Decorative");
    ASSERT_TRUE(decorative.has_value());
    auto workflow = m_mgr->createCategory("CNC Panel", decorative);
    ASSERT_TRUE(workflow.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *workflow));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    auto decor = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Decor" && !c.parentId.has_value();
    });
    ASSERT_NE(decor, categories.end());
    auto panel = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Panel" && c.parentId == decor->id;
    });
    ASSERT_NE(panel, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(panel->id).size(), 1u);
}

TEST_F(LibraryManagerTest, MaintenanceMovesDecorativeRootUnderDecor) {
    auto path = writeMiniSTL("ornament");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto decorative = m_mgr->createCategory("Decorative");
    ASSERT_TRUE(decorative.has_value());
    auto wallArt = m_mgr->createCategory("Wall Art", decorative);
    ASSERT_TRUE(wallArt.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *wallArt));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    EXPECT_EQ(std::count_if(categories.begin(), categories.end(), [](const dw::CategoryRecord& c) {
                  return c.name == "Decorative" && !c.parentId.has_value();
              }),
              0);
    auto decor = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Decor" && !c.parentId.has_value();
    });
    ASSERT_NE(decor, categories.end());
    auto movedWallArt = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Wall Art" && c.parentId == decor->id;
    });
    ASSERT_NE(movedWallArt, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(movedWallArt->id).size(), 1u);
}

TEST_F(LibraryManagerTest, MaintenanceKeepsValidPrimaryDomainWithSameChildName) {
    auto militaryModel = m_mgr->importModel(writeMiniSTL("marine_badge"));
    ASSERT_TRUE(militaryModel.success) << militaryModel.error;
    auto historyModel = m_mgr->importModel(writeDifferentSTL("historical_military"));
    ASSERT_TRUE(historyModel.success) << historyModel.error;

    auto military = m_mgr->createCategory("Military");
    ASSERT_TRUE(military.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(militaryModel.modelId, *military));

    auto historical = m_mgr->createCategory("Historical");
    ASSERT_TRUE(historical.has_value());
    auto childMilitary = m_mgr->createCategory("Military", historical);
    ASSERT_TRUE(childMilitary.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(historyModel.modelId, *childMilitary));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    auto rootMilitary = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Military" && !c.parentId.has_value();
    });
    EXPECT_NE(rootMilitary, categories.end());
    auto historicalMilitary =
        std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
            return c.name == "Military" && c.parentId == historical;
        });
    EXPECT_NE(historicalMilitary, categories.end());
}

TEST_F(LibraryManagerTest, MaintenanceMovesBadgeRootUnderSymbols) {
    auto path = writeMiniSTL("badge_model");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    auto badge = m_mgr->createCategory("Badge");
    ASSERT_TRUE(badge.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(result.modelId, *badge));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    auto symbols = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Symbols" && !c.parentId.has_value();
    });
    ASSERT_NE(symbols, categories.end());
    auto badges = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Badges" && c.parentId == symbols->id;
    });
    ASSERT_NE(badges, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(badges->id).size(), 1u);
    EXPECT_EQ(std::count_if(categories.begin(), categories.end(), [](const dw::CategoryRecord& c) {
                  return c.name == "Badge" && !c.parentId.has_value();
              }),
              0);
}

TEST_F(LibraryManagerTest, MaintenanceMovesSemanticModelRoots) {
    auto animalModel = m_mgr->importModel(writeMiniSTL("animal_model"));
    ASSERT_TRUE(animalModel.success) << animalModel.error;
    auto homeDecorModel = m_mgr->importModel(writeDifferentSTL("home_decor"));
    ASSERT_TRUE(homeDecorModel.success) << homeDecorModel.error;

    auto animalRoot = m_mgr->createCategory("Animal Model");
    ASSERT_TRUE(animalRoot.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(animalModel.modelId, *animalRoot));

    auto homeDecorRoot = m_mgr->createCategory("Home Decor");
    ASSERT_TRUE(homeDecorRoot.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(homeDecorModel.modelId, *homeDecorRoot));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    auto animals = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Animals" && !c.parentId.has_value();
    });
    ASSERT_NE(animals, categories.end());
    auto decor = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Decor" && !c.parentId.has_value();
    });
    ASSERT_NE(decor, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(animals->id).size(), 1u);
    EXPECT_EQ(m_mgr->filterByCategory(decor->id).size(), 1u);
}

TEST_F(LibraryManagerTest, MaintenanceMergesSpecificOneOffRoots) {
    auto religiousModel = m_mgr->importModel(writeMiniSTL("religious"));
    ASSERT_TRUE(religiousModel.success) << religiousModel.error;
    auto wheelModel = m_mgr->importModel(writeDifferentSTL("wheel"));
    ASSERT_TRUE(wheelModel.success) << wheelModel.error;

    auto religious = m_mgr->createCategory("Religious");
    ASSERT_TRUE(religious.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(religiousModel.modelId, *religious));
    auto wheels = m_mgr->createCategory("Wheels");
    ASSERT_TRUE(wheels.has_value());
    ASSERT_TRUE(m_mgr->assignCategory(wheelModel.modelId, *wheels));

    m_mgr->runMaintenance();

    auto categories = m_mgr->getAllCategories();
    auto religion = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Religion" && !c.parentId.has_value();
    });
    ASSERT_NE(religion, categories.end());
    auto vehicles = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Vehicles" && !c.parentId.has_value();
    });
    ASSERT_NE(vehicles, categories.end());
    auto vehicleWheels = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Wheels" && c.parentId == vehicles->id;
    });
    ASSERT_NE(vehicleWheels, categories.end());
}

TEST_F(LibraryManagerTest, ResolveAndAssignCategoriesCanonicalizesInitialAiTagRoots) {
    auto path = writeMiniSTL("ai_tagged_badge");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    ASSERT_TRUE(m_mgr->resolveAndAssignCategories(
        result.modelId, {"3D Print", "Badge", "Military"}));

    auto categories = m_mgr->getAllCategories();
    EXPECT_EQ(std::count_if(categories.begin(), categories.end(), [](const dw::CategoryRecord& c) {
                  return c.name == "3D Print" && !c.parentId.has_value();
              }),
              0);
    auto symbols = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Symbols" && !c.parentId.has_value();
    });
    ASSERT_NE(symbols, categories.end());
    auto badges = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Badges" && c.parentId == symbols->id;
    });
    ASSERT_NE(badges, categories.end());
    auto military = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Military" && c.parentId == badges->id;
    });
    ASSERT_NE(military, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(military->id).size(), 1u);
}

TEST_F(LibraryManagerTest, ResolveAndAssignCategoriesMapsDecorativeInitialAiTagRoot) {
    auto path = writeMiniSTL("ai_tagged_panel");
    auto result = m_mgr->importModel(path);
    ASSERT_TRUE(result.success) << result.error;

    ASSERT_TRUE(m_mgr->resolveAndAssignCategories(
        result.modelId, {"CNC Decorative", "Panel"}));

    auto categories = m_mgr->getAllCategories();
    auto decor = std::find_if(categories.begin(), categories.end(), [](const auto& c) {
        return c.name == "Decor" && !c.parentId.has_value();
    });
    ASSERT_NE(decor, categories.end());
    auto panel = std::find_if(categories.begin(), categories.end(), [&](const auto& c) {
        return c.name == "Panel" && c.parentId == decor->id;
    });
    ASSERT_NE(panel, categories.end());
    EXPECT_EQ(m_mgr->filterByCategory(panel->id).size(), 1u);
}
