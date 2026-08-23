#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "core/config/config.h"

namespace {

namespace fs = std::filesystem;

#ifdef __linux__
class ScopedXdgRuntimeDir {
  public:
    explicit ScopedXdgRuntimeDir(const fs::path& path) {
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

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<fs::path> productionSources() {
    std::vector<fs::path> sources;
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto extension = entry.path().extension().string();
        if (entry.is_regular_file() &&
            (extension == ".h" || extension == ".cpp" || extension == ".cc")) {
            sources.push_back(entry.path());
        }
    }
    return sources;
}

size_t occurrences(const std::string& text, const std::string& token) {
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find(token, position)) != std::string::npos) {
        ++count;
        position += token.size();
    }
    return count;
}

size_t lineCount(const fs::path& path) {
    const auto contents = readFile(path);
    return static_cast<size_t>(std::count(contents.begin(), contents.end(), '\n'));
}

} // namespace

TEST(ProjectLifecycleArchitecture, LegacySetterAndHiddenActivationAreGone) {
    for (const auto& path : productionSources()) {
        const auto contents = readFile(path);
        EXPECT_EQ(contents.find("setCurrentProject"), std::string::npos) << path;
        EXPECT_EQ(contents.find("ensureProjectForModel"), std::string::npos) << path;
    }
}

TEST(ProjectLifecycleArchitecture, GeneralWiringDelegatesAiAndThumbnailOwnership) {
    const auto appRoot = fs::path(CMAKE_SOURCE_DIR) / "src" / "app";
    const auto general = readFile(appRoot / "application_wiring.cpp");
    const auto ai = readFile(appRoot / "application_wiring_ai.cpp");
    const auto thumbnails = readFile(appRoot / "application_wiring_thumbnails.cpp");

    EXPECT_LE(lineCount(appRoot / "application_wiring.cpp"), 500U);
    EXPECT_LE(lineCount(appRoot / "application_wiring_ai.cpp"), 500U);
    EXPECT_LE(lineCount(appRoot / "application_wiring_thumbnails.cpp"), 500U);

    for (const auto* method : {"Application::prepareAiTagging",
                               "Application::wireTagDialog",
                               "Application::handleTagImage"}) {
        EXPECT_EQ(general.find(method), std::string::npos) << method;
        EXPECT_NE(ai.find(method), std::string::npos) << method;
    }
    for (const auto* method : {"Application::regenerateThumbnails",
                               "Application::regenerateSingleThumbnail",
                               "Application::regenerateBatchThumbnails"}) {
        EXPECT_EQ(general.find(method), std::string::npos) << method;
        EXPECT_NE(thumbnails.find(method), std::string::npos) << method;
    }
}

TEST(ProjectLifecycleArchitecture, ManagerSynchronizationHasOneApplicationOwner) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const std::vector<fs::path> allowed = {
        sourceRoot / "app" / "project_session_integration.cpp",
        sourceRoot / "core" / "project" / "project.cpp",
        sourceRoot / "core" / "project" / "project.h",
    };

    for (const auto& path : productionSources()) {
        const auto contents = readFile(path);
        if (contents.find("synchronizeActiveProject") == std::string::npos)
            continue;
        EXPECT_NE(std::find(allowed.begin(), allowed.end(), path), allowed.end()) << path;
    }

    const auto managerSource = readFile(sourceRoot / "core" / "project" / "project.cpp");
    EXPECT_EQ(occurrences(managerSource, "m_currentProject ="), 1U);
    EXPECT_EQ(managerSource.find("m_currentProject.reset"), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, PanelsAndFileIoOnlyEmitLifecycleIntents) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto panel = readFile(sourceRoot / "ui" / "panels" / "project_panel.cpp") +
                       readFile(sourceRoot / "ui" / "panels" / "project_panel_lifecycle.cpp");
    for (const auto& token : {"->create(", "->close(", "synchronizeActiveProject"})
        EXPECT_EQ(panel.find(token), std::string::npos) << token;

    const auto fileIo = readFile(sourceRoot / "managers" / "file_io_manager.cpp") +
                        readFile(sourceRoot / "managers" / "file_io_projects.cpp");
    for (const auto& token : {"synchronizeActiveProject", "ActivateProject", "CloseProject"})
        EXPECT_EQ(fileIo.find(token), std::string::npos) << token;
    EXPECT_EQ(fileIo.find(".detach()"), std::string::npos);
    EXPECT_NE(fileIo.find("showNativeFolder"), std::string::npos);
    EXPECT_NE(fileIo.find("project.json"), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, LibraryFileActionsNeverUseRawStoredParents) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto actions = readFile(sourceRoot / "ui" / "panels" / "library_panel_actions.cpp");

    EXPECT_EQ(actions.find("filePath.parent_path()"), std::string::npos);
    EXPECT_EQ(occurrences(actions, "PathResolver::fileManagerParent("), 2U);
    EXPECT_GE(occurrences(actions, "PathResolver::durableLocation("), 4U);
    EXPECT_GE(occurrences(actions, "PathCategory::Support"), 3U);
}

TEST(ProjectLifecycleArchitecture, PropertiesDisplayDoesNotResolveNetworkLocationPerFrame) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto properties = readFile(sourceRoot / "ui" / "panels" / "properties_panel.cpp");

    EXPECT_EQ(properties.find("PathResolver::resolve("), std::string::npos);
    EXPECT_NE(properties.find("PathResolver::durableLocation("), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, AsyncCompletionsCarrySessionIdentity) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto callbacks = readFile(sourceRoot / "app" / "application_callbacks.cpp");
    EXPECT_EQ(occurrences(callbacks, "modelLoadStillCurrent("), 2U);
    EXPECT_NE(callbacks.find("activeProjectIdentity()"), std::string::npos);

    const auto fileIo = readFile(sourceRoot / "managers" / "file_io_manager.cpp") +
                        readFile(sourceRoot / "managers" / "file_io_projects.cpp");
    EXPECT_GE(occurrences(fileIo, "captureProjectGeneration()"), 3U);
    EXPECT_NE(fileIo.find("expectedProjectGeneration"), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, TemporaryCloseDecisionIsIdentityBoundAndNonDestructive) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto application = readFile(sourceRoot / "app" / "application_project_session.cpp");

    EXPECT_NE(application.find("m_temporaryProjectDecisionPending"), std::string::npos);
    EXPECT_NE(application.find("decisionGeneration"), std::string::npos);
    EXPECT_NE(application.find("current->id() != projectId"), std::string::npos);
    EXPECT_NE(application.find("ProjectTransitionChoice::Discard"), std::string::npos);
    const auto closeStart = application.find("void Application::requestProjectClose");
    const auto closeEnd = application.find("void Application::finishProjectTransition");
    ASSERT_NE(closeStart, std::string::npos);
    ASSERT_NE(closeEnd, std::string::npos);
    EXPECT_EQ(
        application.substr(closeStart, closeEnd - closeStart).find("project->clearModified()"),
        std::string::npos);
}

TEST(ProjectLifecycleArchitecture, FolderImporterCannotActivateOrRewriteProjectSession) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto importer =
        readFile(sourceRoot / "core" / "project" / "project_directory_importer.cpp");

    EXPECT_EQ(importer.find("ProjectSession"), std::string::npos);
    EXPECT_EQ(importer.find("synchronizeActiveProject"), std::string::npos);
    EXPECT_EQ(importer.find("ProjectManager::save"), std::string::npos);
    EXPECT_EQ(importer.find("writeText("), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, HomeIsTheOnlyPanelProjectEntrySurface) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto home = readFile(sourceRoot / "ui" / "panels" / "start_page.cpp");
    const auto projectPanel =
        readFile(sourceRoot / "ui" / "panels" / "project_panel_lifecycle.cpp");

    for (const auto& label : {"New Project", "Open Project", "Recent Projects"}) {
        EXPECT_NE(home.find(label), std::string::npos) << label;
        EXPECT_EQ(projectPanel.find(label), std::string::npos) << label;
    }
    EXPECT_EQ(projectPanel.find("Config::"), std::string::npos);
    EXPECT_NE(projectPanel.find("Open Home"), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, ProjectServicesDoNotOwnHomeVisibility) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto fileIo = readFile(sourceRoot / "managers" / "file_io_manager.h") +
                        readFile(sourceRoot / "managers" / "file_io_projects.cpp");

    EXPECT_EQ(fileIo.find("setShowStartPage"), std::string::npos);
    EXPECT_EQ(fileIo.find("create(\"New Project\")"), std::string::npos);
    EXPECT_NE(fileIo.find("ProjectActivationCompletion"), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, ResumeUsesTypedClosePurposeAndCanonicalGateways) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto resume = readFile(sourceRoot / "app" / "application_project_resume.cpp");
    const auto close = readFile(sourceRoot / "app" / "application_project_session.cpp");
    const auto runtime = readFile(sourceRoot / "app" / "application_runtime.cpp");

    EXPECT_NE(resume.find("requestProjectActivation"), std::string::npos);
    EXPECT_NE(resume.find("activateProjectOpenItem"), std::string::npos);
    EXPECT_NE(resume.find("validateProjectStorage"), std::string::npos);
    EXPECT_NE(close.find("ProjectClosePurpose::ExplicitClose"), std::string::npos);
    EXPECT_NE(runtime.find("ProjectClosePurpose::ApplicationExit"), std::string::npos);
}

TEST(ProjectLifecycleArchitecture, VisibleRoutesAndMachineActionsUseCanonicalGuards) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto resume = readFile(sourceRoot / "app" / "application_project_resume.cpp");
    const auto library = readFile(sourceRoot / "app" / "application_library_picker.cpp");
    const auto lifecycle = readFile(sourceRoot / "app" / "application_project_session.cpp");
    const auto runtime = readFile(sourceRoot / "app" / "application_runtime.cpp");

    EXPECT_NE(resume.find("NavigateWorkshopIntent"), std::string::npos);
    EXPECT_NE(resume.find("WorkshopRoute::Home"), std::string::npos);
    EXPECT_NE(resume.find("showLibrary() = false"), std::string::npos);
    EXPECT_NE(resume.find("showProject() = false"), std::string::npos);
    EXPECT_NE(resume.find("showViewport() = false"), std::string::npos);
    EXPECT_NE(library.find("WorkshopRoute::DesignLibrary"), std::string::npos);
    EXPECT_NE(library.find("ReturnFromLibraryIntent"), std::string::npos);
    EXPECT_NE(runtime.find("handleCompletedLibraryImports(items)"), std::string::npos);
    EXPECT_GE(occurrences(lifecycle, "isStreaming()"), 2U);
}

TEST(ProjectLifecycleArchitecture, NetworkReopenBoundariesSeparateDurableAndLivePaths) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto gcode = readFile(sourceRoot / "ui" / "panels" / "gcode_panel.cpp") +
                       readFile(sourceRoot / "ui" / "panels" / "gcode_panel_files.cpp");
    const auto gcodeHeader = readFile(sourceRoot / "ui" / "panels" / "gcode_panel.h");
    const auto config = readFile(sourceRoot / "core" / "config" / "config.cpp") +
                        readFile(sourceRoot / "core" / "config" / "config_project_paths.cpp");
    const auto projects = readFile(sourceRoot / "managers" / "file_io_projects.cpp");
    const auto projectManager = readFile(sourceRoot / "core" / "project" / "project.cpp");
    const auto projectImporter =
        readFile(sourceRoot / "core" / "project" / "project_directory_importer.cpp");

    EXPECT_NE(gcode.find("PathResolver::durableLocation(requestedPath"), std::string::npos);
    EXPECT_NE(gcode.find("PathResolver::resolve(durablePath"), std::string::npos);
    EXPECT_NE(gcode.find("m_filePath = livePath.string()"), std::string::npos);
    EXPECT_NE(gcode.find("m_durableFilePath = storedPath.string()"), std::string::npos);
    EXPECT_NE(gcode.find("addRecentGCodeFile(storedPath)"), std::string::npos);
    EXPECT_NE(gcode.find("job.filePath = m_durableFilePath"), std::string::npos);
    EXPECT_NE(gcodeHeader.find("std::string m_durableFilePath"), std::string::npos);

    EXPECT_NE(config.find("network_location::durableUrl"), std::string::npos);
    EXPECT_EQ(config.find("PathResolver::"), std::string::npos);
    EXPECT_NE(config.find("!networkLocation && !file::exists(path)"), std::string::npos);

    EXPECT_NE(projects.find("PathResolver::durableLocation(path"), std::string::npos);
    EXPECT_NE(projects.find("PathResolver::resolve(durablePath"), std::string::npos);
    EXPECT_NE(projects.find("directory.open(resolvedPath)"), std::string::npos);
    EXPECT_NE(projects.find("sameProjectPath(durableRecordPath, durablePath)"), std::string::npos);
    EXPECT_NE(projects.find("project->setFilePath(resolvedRecordPath)"), std::string::npos);
    EXPECT_NE(projects.find("project->setFilePath(resolvedPath)"), std::string::npos);

    EXPECT_NE(projectManager.find("ProjectRecord persistedRecord = project.record()"),
              std::string::npos);
    EXPECT_NE(projectManager.find("PathResolver::durableLocation(project.filePath()"),
              std::string::npos);
    EXPECT_NE(projectManager.find("m_projectRepo.update(persistedRecord)"), std::string::npos);
    EXPECT_GE(occurrences(projectManager,
                          "PathResolver::resolve(record->filePath, PathCategory::Projects)"),
              1U);
    EXPECT_NE(projectImporter.find("PathResolver::durableLocation(prepared->manifest.root"),
              std::string::npos);

    const auto projectStorage = readFile(sourceRoot / "core" / "project" / "project_storage.cpp");
    EXPECT_NE(projectStorage.find("PathResolver::durableLocation(root"), std::string::npos);
    EXPECT_NE(projectStorage.find("PathResolver::durableLocation(record.filePath"),
              std::string::npos);
}

TEST(ProjectLifecycleArchitecture, RecentNetworkLocationsRejectUnsafeCandidates) {
    auto& config = dw::Config::instance();
    const auto originalProjects = config.getRecentProjects();
    const auto originalGCode = config.getRecentGCodeFiles();

    config.clearRecentProjects();
    config.clearRecentGCodeFiles();
    config.addRecentProject("smb://alice:secret@nas.local/projects/river-sign");
    config.addRecentGCodeFile("smb://nas.local/gcode/river.nc?token=secret");
    EXPECT_TRUE(config.getRecentProjects().empty());
    EXPECT_TRUE(config.getRecentGCodeFiles().empty());

    for (auto it = originalProjects.rbegin(); it != originalProjects.rend(); ++it)
        config.addRecentProject(*it);
    for (auto it = originalGCode.rbegin(); it != originalGCode.rend(); ++it)
        config.addRecentGCodeFile(*it);
}

#ifdef __linux__
TEST(ProjectLifecycleArchitecture, RecentNetworkLocationsAreCanonicalizedWithoutMounting) {
    auto& config = dw::Config::instance();
    const auto originalProjects = config.getRecentProjects();
    const auto originalGCode = config.getRecentGCodeFiles();

    const auto runtimeRoot = fs::temp_directory_path() / "dw_test_recent_network_runtime";
    fs::remove_all(runtimeRoot);
    ASSERT_TRUE(fs::create_directories(runtimeRoot));
    {
        ScopedXdgRuntimeDir runtimeDir(runtimeRoot);
        ASSERT_TRUE(runtimeDir.valid());

        config.clearRecentProjects();
        config.clearRecentGCodeFiles();
        config.addRecentProject(runtimeRoot / "kio-fuse-old123" / "smb" / "workshop.local" /
                                "Projects" / "River Sign");
        config.addRecentGCodeFile(runtimeRoot / "kio-fuse-old123" / "smb" / "workshop.local" /
                                  "GCode" / "River Sign.nc");

        EXPECT_EQ(config.getRecentProjects().front(),
                  dw::Path("smb://workshop.local/Projects/River%20Sign"));
        EXPECT_EQ(config.getRecentGCodeFiles().front(),
                  dw::Path("smb://workshop.local/GCode/River%20Sign.nc"));

        config.clearRecentProjects();
        config.clearRecentGCodeFiles();
    }

    for (auto it = originalProjects.rbegin(); it != originalProjects.rend(); ++it)
        config.addRecentProject(*it);
    for (auto it = originalGCode.rbegin(); it != originalGCode.rend(); ++it)
        config.addRecentGCodeFile(*it);
    fs::remove_all(runtimeRoot);
}
#endif
