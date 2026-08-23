#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/river_sign_study_fixture.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/library/library_manager.h"
#include "core/utils/file_utils.h"

namespace {

class RiverSignStudyFixtureTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_database));
        m_library = std::make_unique<dw::LibraryManager>(m_database);
        static std::atomic<unsigned long long> sequence{0};
        m_temporary = std::filesystem::temp_directory_path() /
                      ("dw_river_sign_fixture_" +
                       std::to_string(sequence.fetch_add(1)));
        std::filesystem::remove_all(m_temporary);
    }

    void TearDown() override {
        m_library.reset();
        m_database.close();
        std::filesystem::remove_all(m_temporary);
    }

    [[nodiscard]] dw::Path fixtureDirectory() const {
        return dw::Path(CMAKE_SOURCE_DIR) / "tests" / "fixtures" / "ux" /
               "river_sign";
    }

    dw::Database m_database;
    std::unique_ptr<dw::LibraryManager> m_library;
    dw::Path m_temporary;
};

TEST_F(RiverSignStudyFixtureTest,
       SeedsOnlyThreeNamedLibraryModelsWithoutCreatingProjectState) {
    const auto result = dw::river_sign_study::seedLibraryFixture(
        *m_library, fixtureDirectory());

    ASSERT_TRUE(result.seeded()) << result.error;
    EXPECT_TRUE(result.fixture.valid());
    EXPECT_EQ(m_library->modelCount(), 3);

    auto models = m_library->getAllModels();
    std::vector<std::string> names;
    for (const auto& model : models)
        names.push_back(model.name);
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names,
              (std::vector<std::string>{"Alternate", "Preview Only", "Primary"}));

    dw::ProjectRepository projects(m_database);
    dw::GCodeRepository gcodes(m_database);
    EXPECT_EQ(projects.count(), 0);
    EXPECT_EQ(gcodes.count(), 0);
}

TEST_F(RiverSignStudyFixtureTest, RejectsANonemptyLibraryWithoutMutation) {
    dw::ModelRecord existing;
    existing.hash = "existing-study-model";
    existing.name = "Existing";
    existing.filePath = fixtureDirectory() / "river_sign_primary.stl";
    existing.fileFormat = "stl";
    dw::ModelRepository models(m_database);
    ASSERT_TRUE(models.insert(existing).has_value());

    const auto result = dw::river_sign_study::seedLibraryFixture(
        *m_library, fixtureDirectory());

    EXPECT_EQ(result.status,
              dw::river_sign_study::LibrarySeedStatus::LibraryNotEmpty);
    EXPECT_FALSE(result.seeded());
    EXPECT_EQ(m_library->modelCount(), 1);
    ASSERT_EQ(m_library->getAllModels().size(), 1U);
    EXPECT_EQ(m_library->getAllModels().front().name, "Existing");
}

TEST_F(RiverSignStudyFixtureTest, RollsBackEarlierImportsWhenAFixtureIsInvalid) {
    ASSERT_TRUE(dw::file::createDirectories(m_temporary));
    const auto source = fixtureDirectory();
    ASSERT_TRUE(std::filesystem::copy_file(
        source / "river_sign_primary.stl",
        m_temporary / "river_sign_primary.stl"));
    ASSERT_TRUE(std::filesystem::copy_file(
        source / "river_sign_alternate.stl",
        m_temporary / "river_sign_alternate.stl"));
    ASSERT_TRUE(dw::file::writeText(
        m_temporary / "river_sign_preview_only.stl", "not a valid STL"));

    const auto result =
        dw::river_sign_study::seedLibraryFixture(*m_library, m_temporary);

    EXPECT_EQ(result.status,
              dw::river_sign_study::LibrarySeedStatus::ImportFailed);
    EXPECT_FALSE(result.seeded());
    EXPECT_EQ(m_library->modelCount(), 0);
}

TEST_F(RiverSignStudyFixtureTest, MissingFixtureDirectoryFailsClosed) {
    const auto result = dw::river_sign_study::seedLibraryFixture(
        *m_library, m_temporary / "missing");

    EXPECT_EQ(result.status,
              dw::river_sign_study::LibrarySeedStatus::FixtureDirectoryMissing);
    EXPECT_FALSE(result.seeded());
    EXPECT_EQ(m_library->modelCount(), 0);
}

} // namespace
