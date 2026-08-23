#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(CarvePreparationApplicationArchitecture, PinnedDirectoryRequestCannotCreateOrSwitchProject) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto adapter =
        readFile(root / "app" / "application_carve_preparation.cpp");
    const auto requestStart = adapter.find("void Application::requestPinnedProjectDirectory");
    ASSERT_NE(requestStart, std::string::npos);
    const auto request = adapter.substr(requestStart);

    EXPECT_NE(request.find("pin.project()"), std::string::npos);
    EXPECT_NE(request.find("PreparationIdentityPolicy::evaluate"), std::string::npos);
    EXPECT_NE(request.find("currentDirectory()"), std::string::npos);
    for (const auto& forbidden : {
             "->create(", "requestProjectActivation", "addModel", "m_focusedModelId",
             "synchronizeActiveProject", "Direct Carve Project"}) {
        EXPECT_EQ(request.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(CarvePreparationApplicationArchitecture, PreparationBeginsOnlyFromExplicitTypedIntent) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto adapter =
        readFile(root / "app" / "application_carve_preparation.cpp");
    const auto wiring =
        readFile(root / "app" / "application_wiring_workshop.cpp");
    const auto resume =
        readFile(root / "app" / "application_project_resume.cpp");

    EXPECT_NE(adapter.find("bool Application::beginPrepareCarve"), std::string::npos);
    EXPECT_NE(adapter.find("PrepareCarveAdapterStatus::OperationRequired"),
              std::string::npos);
    EXPECT_NE(adapter.find("operation.parentItemId = model.id"), std::string::npos);
    EXPECT_NE(wiring.find("const bool prepareAction"), std::string::npos);
    EXPECT_NE(wiring.find("beginPrepareCarve(*action.target)"), std::string::npos);
    EXPECT_NE(resume.find("resolvePrepareCarvePin("), std::string::npos);
    EXPECT_NE(resume.find("loadOperationOpenItem("), std::string::npos);
}

TEST(CarvePreparationApplicationArchitecture, DirtyPreparationUsesSessionLockAndSaveGate) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto cncWiring =
        readFile(root / "app" / "application_wiring_cnc.cpp");
    const auto sessionWiring =
        readFile(root / "app" / "application_project_session.cpp");
    const auto integration =
        readFile(root / "app" / "project_session_integration.cpp");

    EXPECT_NE(cncWiring.find("setOnPreparationDirty"), std::string::npos);
    EXPECT_NE(cncWiring.find("workshop::SetPreparationLock{dirty}"), std::string::npos);
    EXPECT_NE(cncWiring.find("setCreateProjectRequiredCallback"), std::string::npos);
    EXPECT_NE(sessionWiring.find("savePreparation()"), std::string::npos);
    EXPECT_NE(integration.find("pendingChanges.unsavedPreparation"), std::string::npos);
    EXPECT_NE(integration.find("m_savePreparation()"), std::string::npos);
}

TEST(CarvePreparationApplicationArchitecture, PinnedPanelUsesFlowForEveryPreparationBoundary) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto navigation = readFile(
        root / "ui" / "panels" / "direct_carve_preparation_navigation.cpp");
    const auto adapter = readFile(
        root / "ui" / "panels" / "direct_carve_preparation_adapter.cpp");
    const auto resume = readFile(
        root / "ui" / "panels" / "direct_carve_operation_resume.cpp");
    const auto sync = readFile(
        root / "ui" / "panels" / "direct_carve_project_sync.cpp");
    const auto preview = readFile(
        root / "ui" / "panels" / "direct_carve_carve_preview_step.cpp");

    EXPECT_NE(resume.find("beginPinnedPreparation()"), std::string::npos);
    EXPECT_NE(adapter.find("RefreshPreparation"), std::string::npos);
    EXPECT_NE(adapter.find("GeneratePreparationPreview"), std::string::npos);
    EXPECT_NE(adapter.find("CompletePreparationPreview"), std::string::npos);
    EXPECT_NE(navigation.find("OpenPreparationStage"), std::string::npos);
    EXPECT_NE(navigation.find("ContinuePreparation"), std::string::npos);
    EXPECT_NE(navigation.find("PreparationReady"), std::string::npos);
    EXPECT_NE(adapter.find("SavePreparation"), std::string::npos);
    EXPECT_NE(adapter.find("CompletePreparationSave"), std::string::npos);
    EXPECT_NE(sync.find("EndPreparation"), std::string::npos);
    EXPECT_NE(preview.find("requestPinnedPreviewGeneration()"), std::string::npos);
    EXPECT_NE(preview.find("completePinnedPreviewGeneration(true)"), std::string::npos);
    EXPECT_EQ(navigation.find("Complete or skip the previous step first"),
              std::string::npos);
    EXPECT_LT(std::count(navigation.begin(), navigation.end(), '\n'), 500);
}
