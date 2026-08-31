#include <gtest/gtest.h>

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/schema.h"
#include "core/library/library_manager.h"
#include "core/mesh/hash.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/project/project_directory_importer.h"
#include "core/utils/file_utils.h"

namespace dw {
namespace {

#ifdef __linux__
class ScopedXdgRuntimeDir {
  public:
    explicit ScopedXdgRuntimeDir(const Path& path) {
        if (const char* current = std::getenv("XDG_RUNTIME_DIR"))
            m_previous = current;
        m_valid = setenv("XDG_RUNTIME_DIR", path.c_str(), 1) == 0;
    }

    ~ScopedXdgRuntimeDir() {
        if (!m_valid)
            return;
        if (m_previous)
            (void)setenv("XDG_RUNTIME_DIR", m_previous->c_str(), 1);
        else
            (void)unsetenv("XDG_RUNTIME_DIR");
    }

    bool valid() const { return m_valid; }

  private:
    std::optional<std::string> m_previous;
    bool m_valid = false;
};
#endif

class ProjectDirectoryImporterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_tempRoot = std::filesystem::temp_directory_path() / "dw_test_project_directory_importer";
        std::filesystem::remove_all(m_tempRoot);
        ASSERT_TRUE(std::filesystem::create_directories(m_tempRoot / "source"));

        m_oldGCodeDir = Config::instance().getGCodeDir();
        Config::instance().setGCodeDir(m_tempRoot / "global-gcode");

        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(Schema::initialize(m_database));
        m_library = std::make_unique<LibraryManager>(m_database);
        m_gcode = std::make_unique<GCodeRepository>(m_database);
        m_projects = std::make_unique<ProjectManager>(m_database);
    }

    void TearDown() override {
        m_projects.reset();
        m_gcode.reset();
        m_library.reset();
        Config::instance().setGCodeDir(m_oldGCodeDir);
        std::filesystem::remove_all(m_tempRoot);
    }

    Path writeModel() {
        const Path path = m_tempRoot / "source" / "river-sign.stl";
        ByteBuffer bytes(80U + 4U + 50U, 0U);
        const u32 triangleCount = 1;
        std::memcpy(bytes.data() + 80U, &triangleCount, sizeof(triangleCount));
        const float triangle[12] = {
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            0.0F,
            0.0F,
            10.0F,
            0.0F,
            0.0F,
            0.0F,
            10.0F,
            0.0F,
        };
        std::memcpy(bytes.data() + 84U, triangle, sizeof(triangle));
        EXPECT_TRUE(file::writeBinary(path, bytes));
        return path;
    }

    Path writeGCode(
        const std::string& contents = "G21\nG90\nG0 X0 Y0 Z5\nG1 X10 Y0 Z0 F100\nM30\n") {
        const Path path = m_tempRoot / "source" / "river-sign.nc";
        EXPECT_TRUE(file::writeText(path, contents));
        return path;
    }

    void buildValidProject(bool includeGCode = true) {
        ASSERT_TRUE(
            m_directory.create(m_tempRoot / "project", "River Sign", "A beginner sign project"));
        ASSERT_TRUE(m_directory.addModelFile(writeModel()));
        if (includeGCode) {
            ASSERT_TRUE(m_directory.addGCodeFile(writeGCode(), "1/8 inch end mill"));
        }
        ASSERT_TRUE(m_directory.save());
    }

    nlohmann::json manifest() const {
        auto text = file::readText(m_tempRoot / "project" / "project.json");
        EXPECT_TRUE(text.has_value());
        return text ? nlohmann::json::parse(*text) : nlohmann::json::object();
    }

    void writeManifest(const nlohmann::json& value) {
        ASSERT_TRUE(file::writeText(m_tempRoot / "project" / "project.json", value.dump(2)));
    }

    ProjectDirectory reopenDirectory() const {
        ProjectDirectory reopened;
        EXPECT_TRUE(reopened.open(m_tempRoot / "project"));
        return reopened;
    }

    ProjectDirectoryImportResult hydrate(const ProjectDirectory& directory) {
        ProjectDirectoryImporter importer(m_database, *m_library);
        return importer.hydrate(directory);
    }

    Database m_database;
    std::unique_ptr<LibraryManager> m_library;
    std::unique_ptr<GCodeRepository> m_gcode;
    std::unique_ptr<ProjectManager> m_projects;
    ProjectDirectory m_directory;
    Path m_tempRoot;
    Path m_oldGCodeDir;
};

TEST_F(ProjectDirectoryImporterTest, HydratesModelsGCodeAndMetadataWithoutActivating) {
    buildValidProject();

    auto result = hydrate(m_directory);

    ASSERT_TRUE(result.success()) << result.message;
    ASSERT_TRUE(result.projectId.has_value());
    auto reopened = m_projects->open(*result.projectId);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->name(), "River Sign");
    EXPECT_EQ(reopened->description(), "A beginner sign project");
    EXPECT_EQ(reopened->filePath(), std::filesystem::canonical(m_tempRoot / "project"));
    EXPECT_EQ(result.modelCount, 1U);
    EXPECT_EQ(result.gcodeCount, 1U);
    EXPECT_EQ(m_projects->currentProject(), nullptr);
    EXPECT_EQ(m_projects->currentDirectory(), nullptr);

    EXPECT_EQ(reopened->modelCount(), 1);
    auto linkedGCode = m_gcode->findByProject(*result.projectId);
    ASSERT_EQ(linkedGCode.size(), 1U);
    EXPECT_GT(linkedGCode[0].totalDistance, 0.0F);
    const Path storedPath = PathResolver::resolve(linkedGCode[0].filePath, PathCategory::GCode);
    EXPECT_TRUE(file::isFile(storedPath));
    EXPECT_EQ(storedPath.parent_path(), m_tempRoot / "global-gcode");
}

#ifdef __linux__
TEST_F(ProjectDirectoryImporterTest, PersistsDurableNetworkProjectLocation) {
    ScopedXdgRuntimeDir runtimeDir(m_tempRoot);
    ASSERT_TRUE(runtimeDir.valid());
    const Path networkRoot = m_tempRoot / "kio-fuse-old123" / "smb" /
                             "workshop.local" / "Projects" / "river-sign";
    ProjectDirectory networkDirectory;
    ASSERT_TRUE(networkDirectory.create(
        networkRoot, "River Sign", "A network-backed beginner project"));
    ASSERT_TRUE(networkDirectory.addModelFile(writeModel()));
    ASSERT_TRUE(networkDirectory.save());

    auto result = hydrate(networkDirectory);

    ASSERT_TRUE(result.success()) << result.message;
    ASSERT_TRUE(result.projectId.has_value());
    auto record = m_projects->getProjectInfo(*result.projectId);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->filePath,
              Path("smb://workshop.local/Projects/river-sign"));
    EXPECT_EQ(result.canonicalRoot, std::filesystem::canonical(networkRoot));
}
#endif

TEST_F(ProjectDirectoryImporterTest, AdoptsForeignManifestOwnerOnFirstLocalSave) {
    buildValidProject();
    constexpr i64 foreignProjectId = 987654;
    m_directory.setProjectId(foreignProjectId);
    ASSERT_TRUE(m_directory.save());

    auto result = hydrate(reopenDirectory());

    ASSERT_TRUE(result.success()) << result.message;
    ASSERT_TRUE(result.projectId.has_value());
    ASSERT_NE(*result.projectId, foreignProjectId);
    auto imported = m_projects->open(*result.projectId);
    ASSERT_TRUE(imported);

    m_projects->synchronizeActiveProject(imported);
    ASSERT_TRUE(m_projects->currentDirectory());
    EXPECT_EQ(m_projects->currentDirectory()->projectId(), foreignProjectId);
    ASSERT_TRUE(m_projects->save(*imported));

    ProjectDirectory adopted;
    ASSERT_TRUE(adopted.open(m_tempRoot / "project"));
    EXPECT_EQ(adopted.projectId(), *result.projectId);
}

TEST_F(ProjectDirectoryImporterTest, ReusesExistingAssetsByHash) {
    buildValidProject();
    auto first = hydrate(m_directory);
    ASSERT_TRUE(first.success()) << first.message;
    auto firstProject = m_projects->open(*first.projectId);
    ASSERT_TRUE(firstProject);
    const i64 firstModelId = firstProject->modelIds().front();
    const i64 firstGCodeId = m_gcode->findByProject(*first.projectId).front().id;

    auto second = hydrate(reopenDirectory());

    ASSERT_TRUE(second.success()) << second.message;
    auto secondProject = m_projects->open(*second.projectId);
    ASSERT_TRUE(secondProject);
    ASSERT_EQ(secondProject->modelIds().size(), 1U);
    EXPECT_EQ(secondProject->modelIds().front(), firstModelId);
    ASSERT_EQ(m_gcode->findByProject(*second.projectId).size(), 1U);
    EXPECT_EQ(m_gcode->findByProject(*second.projectId).front().id, firstGCodeId);
    EXPECT_EQ(m_library->modelCount(), 1);
    EXPECT_EQ(m_gcode->count(), 1);
}

TEST_F(ProjectDirectoryImporterTest, RejectsTraversalBeforeDatabaseMutation) {
    buildValidProject(false);
    const Path escaped = m_tempRoot / "project" / "escaped.stl";
    ASSERT_TRUE(file::copy(writeModel(), escaped));
    auto json = manifest();
    json["models"][0]["filename"] = "../escaped.stl";
    json["models"][0]["hash"] = hash::computeFile(escaped);
    writeManifest(json);

    auto result = hydrate(reopenDirectory());

    EXPECT_EQ(result.error, ProjectDirectoryImportError::UnsafeEntry);
    EXPECT_TRUE(m_projects->listProjects().empty());
    EXPECT_EQ(m_library->modelCount(), 0);
}

TEST_F(ProjectDirectoryImporterTest, RejectsMissingManifestEntry) {
    buildValidProject(false);
    auto json = manifest();
    json["models"][0]["filename"] = "missing.stl";
    writeManifest(json);

    auto result = hydrate(reopenDirectory());

    EXPECT_EQ(result.error, ProjectDirectoryImportError::MissingEntry);
    EXPECT_TRUE(m_projects->listProjects().empty());
}

TEST_F(ProjectDirectoryImporterTest, RejectsModelHashMismatch) {
    buildValidProject(false);
    auto json = manifest();
    json["models"][0]["hash"] = "0000000000000000";
    writeManifest(json);

    auto result = hydrate(reopenDirectory());

    EXPECT_EQ(result.error, ProjectDirectoryImportError::HashMismatch);
    EXPECT_TRUE(m_projects->listProjects().empty());
    EXPECT_EQ(m_library->modelCount(), 0);
}

TEST_F(ProjectDirectoryImporterTest, RejectsInvalidGCodeBeforeImport) {
    buildValidProject();
    auto json = manifest();
    const std::string filename = json["gcode"][0]["filename"].get<std::string>();
    ASSERT_TRUE(
        file::writeText(m_tempRoot / "project" / "gcode" / filename, "this is not a toolpath\n"));

    auto result = hydrate(reopenDirectory());

    EXPECT_EQ(result.error, ProjectDirectoryImportError::InvalidGCode);
    EXPECT_TRUE(m_projects->listProjects().empty());
    EXPECT_EQ(m_gcode->count(), 0);
}

TEST_F(ProjectDirectoryImporterTest, AssociationFailureRemovesNewProjectRecord) {
    buildValidProject();
    ASSERT_TRUE(
        m_database.execute("CREATE TRIGGER fail_project_gcode BEFORE INSERT ON project_gcode "
                           "BEGIN SELECT RAISE(FAIL, 'forced association failure'); END"));

    auto result = hydrate(m_directory);

    EXPECT_EQ(result.error, ProjectDirectoryImportError::ProjectAssociationFailed);
    EXPECT_TRUE(m_projects->listProjects().empty());
}

TEST_F(ProjectDirectoryImporterTest, ProjectInsertFailureRollsBackImportedAssets) {
    buildValidProject();
    ASSERT_TRUE(m_database.execute("CREATE TRIGGER fail_project_insert BEFORE INSERT ON projects "
                                   "BEGIN SELECT RAISE(FAIL, 'forced insert failure'); END"));

    auto result = hydrate(m_directory);

    EXPECT_EQ(result.error, ProjectDirectoryImportError::ProjectCreateFailed);
    EXPECT_TRUE(m_projects->listProjects().empty());
    EXPECT_EQ(m_library->modelCount(), 0);
    EXPECT_EQ(m_gcode->count(), 0);
}

} // namespace
} // namespace dw
