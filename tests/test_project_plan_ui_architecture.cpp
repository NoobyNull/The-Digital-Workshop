#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.is_open()) << path;
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string projectPanelSources() {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    return readFile(root / "project_panel.h") +
           readFile(root / "project_panel.cpp") +
           readFile(root / "project_panel_lifecycle.cpp");
}

} // namespace

TEST(ProjectPlanUiArchitecture, LegacySectionsAndSourceSpecificCallbacksAreGone) {
    const auto panel = projectPanelSources();

    for (const auto* token : {
             "Work Order",
             "renderWarningsSection",
             "renderOpenItemsSection",
             "renderModelsSection",
             "renderGCodeSection",
             "renderMaterialsSection",
             "renderCostsSection",
             "renderCutPlansSection",
             "ModelSelectedCallback",
             "GCodeSelectedCallback",
             "MaterialSelectedCallback",
             "CostSelectedCallback",
             "CutPlanSelectedCallback",
             "OperationSelectedCallback",
             "setOnModelSelected",
             "setOnGCodeSelected",
             "setOnMaterialSelected",
             "setOnCostSelected",
             "setOnCutPlanSelected",
             "setOnOperationSelected",
             "setSelectedModelId",
         }) {
        EXPECT_EQ(panel.find(token), std::string::npos) << token;
    }
}

TEST(ProjectPlanUiArchitecture, PanelConsumesSnapshotsAndDoesNotMutateRepositories) {
    const auto panel = projectPanelSources();

    EXPECT_NE(panel.find("ProjectPanelSnapshot"), std::string::npos);
    EXPECT_NE(panel.find("ProjectPlanProvider"), std::string::npos);
    for (const auto* token : {
             "ProjectManager",
             "ProjectRepository",
             "ModelRepository",
             "GCodeRepository",
             "MaterialRepository",
             "CutPlanRepository",
             "CostRepository",
             "m_projectManager",
             "m_modelRepo",
             "m_gcodeRepo",
             "m_cutPlanRepo",
             "m_costRepo",
             "currentProject()",
             "record()",
             "markModified()",
             "removeModel(",
             "removeFromProject(",
         }) {
        EXPECT_EQ(panel.find(token), std::string::npos) << token;
    }
}

TEST(ProjectPlanUiArchitecture, OnePlanSurfaceExposesProgressActionsAndTextRoles) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto panel = projectPanelSources();
    const auto view = readFile(sourceRoot / "ui" / "panels" / "project_plan_view.cpp");

    EXPECT_NE(panel.find("renderProjectPlanView"), std::string::npos);
    for (const auto* label : {
             "Continue",
             "Project Plan",
             "Project Items",
             "Information:",
         }) {
        EXPECT_NE(view.find(label), std::string::npos) << label;
    }
    EXPECT_EQ(view.find("Add from Design Library"), std::string::npos);
    EXPECT_NE(view.find("stageStateLabel(stage.state)"), std::string::npos);
    EXPECT_NE(view.find("requiredWidth"), std::string::npos);
    EXPECT_NE(view.find("ImGui::CalcTextSize(stageText.c_str())"),
              std::string::npos);
    EXPECT_NE(view.find("Status: %s"), std::string::npos);
    EXPECT_NE(view.find("itemStateLabel(node.item.state)"), std::string::npos);
    EXPECT_NE(view.find("nodeRoleLabel(node.role)"), std::string::npos);
    EXPECT_NE(view.find("callbacks.onActivateItem(node.item.ref)"), std::string::npos);
    EXPECT_NE(view.find("renderNode(plan, *child"), std::string::npos);
}

TEST(ProjectPlanUiArchitecture, CompactProjectTreePreservesSiblingIdentity) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto view = readFile(
        sourceRoot / "ui" / "panels" / "project_plan_view.cpp");
    const auto lifecycle = readFile(
        sourceRoot / "ui" / "panels" / "project_panel_lifecycle.cpp");

    EXPECT_EQ(view.find("ImGuiTreeNodeFlags_DefaultOpen"), std::string::npos);
    EXPECT_NE(view.find("ImGuiTreeNodeFlags_OpenOnArrow"), std::string::npos);
    EXPECT_NE(view.find("responsiveNodeLabel"), std::string::npos);
    EXPECT_NE(view.find("ImGui::TextWrapped(\"%s\", responsiveLabel.detail"),
              std::string::npos);
    EXPECT_NE(view.find("ImGui::TextWrapped(\"%s\", metadata.c_str())"),
              std::string::npos);
    EXPECT_NE(lifecycle.find("Design: %s"), std::string::npos);
    EXPECT_NE(lifecycle.find("Legacy project designs: %s"),
              std::string::npos);
    EXPECT_NE(lifecycle.find("\"Selected: \" + node.item.label"),
              std::string::npos);
    EXPECT_NE(lifecycle.find("ImGui::TextWrapped(\"%s\", selected.c_str())"),
              std::string::npos);
}

TEST(ProjectPlanUiArchitecture, GenericActivationChecksExactIdentityAndRepairIsDistinct) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto wiring =
        readFile(sourceRoot / "app" / "application_wiring_workshop.cpp");

    EXPECT_NE(wiring.find("workshop::ProjectItemRef ref"), std::string::npos);
    EXPECT_NE(wiring.find("ref.valid()"), std::string::npos);
    EXPECT_NE(wiring.find("findOpenItem(ref.item.value)"), std::string::npos);
    EXPECT_NE(wiring.find("item->projectId != ref.project.value"), std::string::npos);
    EXPECT_NE(wiring.find("activateProjectOpenItem(*item)"), std::string::npos);
    EXPECT_NE(wiring.find("setOnProjectItemActivated([activateRef]"),
              std::string::npos);

    const auto actionStart = wiring.find("setOnProjectPlanAction");
    const auto actionEnd = wiring.find("setOpenHomeCallback", actionStart);
    ASSERT_NE(actionStart, std::string::npos);
    ASSERT_NE(actionEnd, std::string::npos);
    const auto actions = wiring.substr(actionStart, actionEnd - actionStart);
    const auto repair = actions.find("action.kind == NextActionKind::RepairItem");
    const auto ordinaryActivation =
        actions.find("activateRef(*action.target)", repair);
    ASSERT_NE(repair, std::string::npos);
    ASSERT_NE(ordinaryActivation, std::string::npos);
    ASSERT_LT(repair, ordinaryActivation);

    const auto repairRoute = actions.substr(repair, ordinaryActivation - repair);
    EXPECT_NE(repairRoute.find("Project Item Needs Repair"), std::string::npos);
    EXPECT_NE(repairRoute.find("return;"), std::string::npos);
    EXPECT_EQ(repairRoute.find("activateProjectOpenItem"), std::string::npos);
}

TEST(ProjectPlanUiArchitecture, ApplicationOverlaysLivePreparationAndExactRunTruth) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto wiring =
        readFile(sourceRoot / "app" / "application_wiring_workshop.cpp");
    const auto runTruth = readFile(
        sourceRoot / "app" / "project_plan_run_truth_adapter.cpp");
    const auto advancedSource = readFile(
        sourceRoot / "ui" / "panels" / "gcode_panel_run_truth.cpp");
    const auto application = readFile(sourceRoot / "app" / "application.cpp");
    const auto preparation = readFile(
        sourceRoot / "ui" / "panels" /
        "direct_carve_preparation_adapter.cpp");

    EXPECT_NE(preparation.find("DirectCarvePanel::projectPlanSnapshot"),
              std::string::npos);
    EXPECT_NE(preparation.find("workflowState()"), std::string::npos);
    EXPECT_NE(wiring.find("m_directCarveRunEffectAdapter->snapshot()"),
              std::string::npos);
    EXPECT_NE(wiring.find("gcodePanel->projectPlanRunSnapshot()"),
              std::string::npos);
    EXPECT_NE(wiring.find("m_projectPlanRunTruthAdapter->resolve"),
              std::string::npos);
    EXPECT_NE(wiring.find("input.liveRun = runTruth.snapshot"),
              std::string::npos);

    // Identity matching lives in one reporting-only adapter, not in the UI
    // wiring. Both paths resolve the exact persisted job -> program hierarchy.
    EXPECT_EQ(wiring.find("item.sourceTable == \"cnc_jobs\""),
              std::string::npos);
    EXPECT_NE(runTruth.find("item.sourceTable == \"cnc_jobs\""),
              std::string::npos);
    EXPECT_NE(runTruth.find("sourceTable != \"gcode_files\""),
              std::string::npos);
    EXPECT_NE(runTruth.find("ConflictingActiveSources"), std::string::npos);
    EXPECT_NE(runTruth.find("operation != expectedOperation"),
              std::string::npos);

    // Advanced sender pause truth comes from controller stream state, while
    // startup interruption is recorded only for rows this launch recovered.
    EXPECT_NE(advancedSource.find("m_activeJobId"), std::string::npos);
    EXPECT_NE(advancedSource.find("m_cnc->isStreaming()"), std::string::npos);
    EXPECT_NE(advancedSource.find("m_cnc->isHeld()"), std::string::npos);
    EXPECT_NE(application.find("rememberInterruptedJob(job.id)"),
              std::string::npos);
}

TEST(ProjectPlanUiArchitecture, RunActionsRouteUnderlyingContentWithoutSelectingJob) {
    const auto sourceRoot = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto wiring =
        readFile(sourceRoot / "app" / "application_wiring_workshop.cpp");
    const auto runStart = wiring.find("const bool runAction");
    const auto genericActivation = wiring.find(
        "if (action.target) (void)activateRef(*action.target)", runStart);
    ASSERT_NE(runStart, std::string::npos);
    ASSERT_NE(genericActivation, std::string::npos);
    const auto runRoute = wiring.substr(runStart, genericActivation - runStart);

    EXPECT_NE(runRoute.find("resolveProjectPlanRunActionRoute"),
              std::string::npos);
    EXPECT_NE(runRoute.find("action.kind == NextActionKind::MonitorRun"),
              std::string::npos);
    EXPECT_NE(runRoute.find("*direct.jobId == route.jobSourceId"),
              std::string::npos);
    EXPECT_NE(runRoute.find("advanced->jobSourceId == route.jobSourceId"),
              std::string::npos);
    EXPECT_NE(runRoute.find("beginPrepareCarve(*route.operation)"),
              std::string::npos);
    EXPECT_NE(runRoute.find("activateRef(route.program)"),
              std::string::npos);
    EXPECT_EQ(runRoute.find("activateRef(*action.target)"),
              std::string::npos);
}
