#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

TEST(RiverSignStudyArchitecture,
     ExplicitNormalBuildPathProvesFreshInteractiveReadinessWithoutTaskAutomation) {
    const auto root = std::filesystem::path(CMAKE_SOURCE_DIR);
    const auto main = readFile(root / "src" / "main.cpp");
    const auto command =
        readFile(root / "src" / "app" / "river_sign_study_command.cpp");
    const auto application =
        readFile(root / "src" / "app" / "application_river_sign_study.cpp");
    const auto header = readFile(root / "src" / "app" / "application.h");
    const auto cmake = readFile(root / "src" / "CMakeLists.txt");

    EXPECT_NE(command.find("--river-sign-study"), std::string::npos);
    EXPECT_NE(main.find("river_sign_study::parseCommand"), std::string::npos);
    EXPECT_NE(main.find("app.runRiverSignStudy"), std::string::npos);
    EXPECT_NE(header.find("int runRiverSignStudy(const Path& fixtureDirectory);"),
              std::string::npos);

    const auto studySource =
        cmake.find("app/application_river_sign_study.cpp");
    const auto captureConditional = cmake.find("if(DW_ENABLE_UX_CAPTURE)");
    ASSERT_NE(studySource, std::string::npos);
    ASSERT_NE(captureConditional, std::string::npos);
    EXPECT_LT(studySource, captureConditional);

    for (const auto& required : {
             "listProjects().empty()",
             "modelCount() != 0",
             "m_gcodeRepo->count() != 0",
             "seedLibraryFixture",
             "setGuidedEnabled(true)",
             "LibraryPickerPurpose::StartProject",
             "WorkshopRoute::DesignLibrary",
             "projectMembership.empty()",
             "selectedItems.empty()",
             "isSimulating()",
             "DW_STUDY_READY=",
             "return run();",
         }) {
        EXPECT_NE(application.find(required), std::string::npos) << required;
    }
    for (const auto& forbidden : {
             "ConfirmLibrarySelection",
             "ReplaceLibrarySelection",
             "NamedProjectCreationService",
             "score_river_sign_study",
             "human_study_status",
         }) {
        EXPECT_EQ(application.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(RiverSignStudyArchitecture,
     CaptureAndStudyStartupShareOnlyTheLibraryFixtureSeeder) {
    const auto root = std::filesystem::path(CMAKE_SOURCE_DIR) / "src" / "app";
    const auto fixture = readFile(root / "river_sign_study_fixture.cpp");
    const auto capture = readFile(root / "ux_capture_fixture.cpp");

    EXPECT_NE(capture.find("river_sign_study::seedLibraryFixture"),
              std::string::npos);
    EXPECT_EQ(capture.find("library.importModel"), std::string::npos);
    EXPECT_EQ(fixture.find("ProjectManager"), std::string::npos);
    EXPECT_EQ(fixture.find("NamedProjectCreationService"), std::string::npos);
    EXPECT_EQ(fixture.find("ProjectAssetMembershipService"), std::string::npos);
}

TEST(RiverSignStudyArchitecture, AdvancedRemainsTheOrdinaryProductDefault) {
    const auto root = std::filesystem::path(CMAKE_SOURCE_DIR) / "src" / "core" /
                      "config";
    const auto config = readFile(root / "config.h");
    const auto migration = readFile(root / "layout_migration.cpp");

    EXPECT_NE(config.find("int m_activeLayoutPresetIndex = 1;"),
              std::string::npos);
    EXPECT_NE(migration.find("result.activePresetIndex = 1;"),
              std::string::npos);
}

TEST(RiverSignStudyArchitecture,
     FreshStartupDoesNotManufactureAPreviousImportLog) {
    const auto application = readFile(
        std::filesystem::path(CMAKE_SOURCE_DIR) / "src" / "app" /
        "application.cpp");

    EXPECT_EQ(
        application.find("file::touch(cfg.getSupportDir() / \".import-log\")"),
        std::string::npos);
    EXPECT_NE(application.find("if (m_importLog->exists())"),
              std::string::npos);
}

TEST(RiverSignStudyArchitecture,
     ProcessSignalsAreDeferredToTheApplicationMainLoop) {
    const auto root = std::filesystem::path(CMAKE_SOURCE_DIR);
    const auto main = readFile(root / "src" / "main.cpp");
    const auto application = readFile(root / "src" / "app" / "application.h");
    const auto runtime =
        readFile(root / "src" / "app" / "application_runtime.cpp");

    EXPECT_NE(main.find("volatile std::sig_atomic_t g_terminationRequested"),
              std::string::npos);
    EXPECT_NE(main.find("g_terminationRequested = signal;"), std::string::npos);
    EXPECT_NE(main.find("setTerminationSignalFlag(&g_terminationRequested)"),
              std::string::npos);
    EXPECT_EQ(main.find("g_app->quit()"), std::string::npos);
    EXPECT_NE(application.find("setTerminationSignalFlag"), std::string::npos);
    EXPECT_NE(runtime.find("*m_terminationSignalFlag = 0;"), std::string::npos);
    EXPECT_NE(runtime.find("quit();"), std::string::npos);
}

} // namespace
