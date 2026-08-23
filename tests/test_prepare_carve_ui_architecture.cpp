#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "ui/panels/direct_carve_layout_policy.h"
#include "ui/panels/direct_carve_step_indicator_policy.h"

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t lineCount(const fs::path& path) {
    std::ifstream input(path);
    return static_cast<std::size_t>(
        std::count(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>(), '\n'));
}

} // namespace

TEST(PrepareCarveUiArchitecture, FirstFourStagesHaveOneMaterialBeforeToolAuthority) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto header = readFile(root / "ui" / "panels" / "direct_carve_panel.h");
    const auto navigation =
        readFile(root / "ui" / "panels" / "direct_carve_preparation_navigation.cpp");

    const auto stepEnumBegin = header.find("enum class Step {");
    ASSERT_NE(stepEnumBegin, std::string::npos);
    const auto stepEnumEnd = header.find("};", stepEnumBegin);
    ASSERT_NE(stepEnumEnd, std::string::npos);
    const auto stepEnum = header.substr(stepEnumBegin, stepEnumEnd - stepEnumBegin);

    const auto model = stepEnum.find("ModelFit,");
    const auto material = stepEnum.find("MaterialSetup,");
    const auto tool = stepEnum.find("ToolSelect,");
    const auto preview = stepEnum.find("Preview,");
    ASSERT_NE(model, std::string::npos);
    ASSERT_NE(material, std::string::npos);
    ASSERT_NE(tool, std::string::npos);
    ASSERT_NE(preview, std::string::npos);
    EXPECT_LT(model, material);
    EXPECT_LT(material, tool);
    EXPECT_LT(tool, preview);

    EXPECT_NE(navigation.find("OpenPreparationStage"), std::string::npos);
    EXPECT_NE(navigation.find("ContinuePreparation"), std::string::npos);
    EXPECT_NE(navigation.find("PreparationReady"), std::string::npos);
    EXPECT_NE(navigation.find("pinnedPlanning"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, EveryFlowBoundaryIntentIsWiredWithoutCncEffects) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto adapter = readFile(root / "ui" / "panels" / "direct_carve_preparation_adapter.cpp");
    const auto navigation =
        readFile(root / "ui" / "panels" / "direct_carve_preparation_navigation.cpp");
    const auto resume = readFile(root / "ui" / "panels" / "direct_carve_operation_resume.cpp");
    const auto sync = readFile(root / "ui" / "panels" / "direct_carve_project_sync.cpp");
    const auto combined = adapter + navigation + resume + sync;

    for (const auto& token : {"BeginPreparation",
                              "RefreshPreparation",
                              "OpenPreparationStage",
                              "ContinuePreparation",
                              "GeneratePreparationPreview",
                              "CompletePreparationPreview",
                              "SavePreparation",
                              "CompletePreparationSave",
                              "EndPreparation"}) {
        EXPECT_NE(combined.find(token), std::string::npos) << token;
    }
    EXPECT_EQ(adapter.find("sendCommand"), std::string::npos);
    EXPECT_EQ(adapter.find("startStreaming"), std::string::npos);
    EXPECT_EQ(navigation.find("sendCommand"), std::string::npos);
    EXPECT_EQ(navigation.find("startStreaming"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ToolRoleChildrenAreReconciledByExactOwnedSourceKey) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto sync = readFile(root / "ui" / "panels" / "direct_carve_project_sync.cpp");
    const auto toolChildren = readFile(root / "ui" / "panels" / "direct_carve_tool_child_sync.cpp");

    EXPECT_NE(sync.find("reconcileToolOpenItems()"), std::string::npos);
    EXPECT_NE(toolChildren.find("removeToolOpenItems(\"finish\")"), std::string::npos);
    EXPECT_NE(toolChildren.find("removeToolOpenItems(\"clear\")"), std::string::npos);
    EXPECT_NE(toolChildren.find("case carve::ClearingToolMode::Automatic:"), std::string::npos);
    EXPECT_NE(toolChildren.find("case carve::ClearingToolMode::Selected:"), std::string::npos);
    EXPECT_NE(toolChildren.find("case carve::ClearingToolMode::Disabled:"), std::string::npos);
    EXPECT_NE(toolChildren.find("item.sourceKey == roleSourceKey"), std::string::npos);
    EXPECT_NE(toolChildren.find("*item.parentItemId == operation->id"), std::string::npos);
    EXPECT_NE(toolChildren.find("m_projectManager->removeOpenItem(item.id)"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, StandaloneAdvancedPathNeverInventsProjectIdentity) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto adapter = readFile(root / "ui" / "panels" / "direct_carve_preparation_adapter.cpp");
    const auto context = readFile(root / "ui" / "panels" / "direct_carve_preparation_context.cpp");

    EXPECT_NE(adapter.find("if (!m_preparationPin) return true"), std::string::npos);
    EXPECT_NE(context.find("Standalone preparation"), std::string::npos);
    EXPECT_NE(context.find("Create Project"), std::string::npos);
    EXPECT_EQ(adapter.find("PrepareCarvePin{"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, NavigationExtractionMeetsSessionSizeTargets) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    EXPECT_LE(lineCount(root / "direct_carve_preparation_navigation.cpp"), 500U);
    EXPECT_LE(lineCount(root / "direct_carve_step_indicator.cpp"), 500U);
    EXPECT_LE(lineCount(root / "direct_carve_preparation_adapter.cpp"), 500U);
    EXPECT_LE(lineCount(root / "direct_carve_project_sync.cpp"), 500U);
    EXPECT_LE(lineCount(root / "direct_carve_tool_child_sync.cpp"), 500U);
    EXPECT_LE(lineCount(root / "direct_carve_panel.cpp"), 750U);

    for (const auto* step : {"direct_carve_design_size_step.cpp",
                             "direct_carve_material_blank_step.cpp",
                             "direct_carve_choose_tool_step.cpp",
                             "direct_carve_tool_library_picker.cpp",
                             "direct_carve_carve_preview_step.cpp"}) {
        EXPECT_LE(lineCount(root / step), 500U) << step;
    }

    const auto panel = readFile(root / "direct_carve_panel.cpp");
    EXPECT_EQ(panel.find("void DirectCarvePanel::navigateToStep"), std::string::npos);
    EXPECT_EQ(panel.find("void DirectCarvePanel::renderStepIndicator"), std::string::npos);
    EXPECT_EQ(panel.find("void DirectCarvePanel::renderModelFit"), std::string::npos);
    EXPECT_EQ(panel.find("void DirectCarvePanel::renderMaterialSetup"), std::string::npos);
    EXPECT_EQ(panel.find("void DirectCarvePanel::renderToolSelect"), std::string::npos);
    EXPECT_EQ(panel.find("void DirectCarvePanel::renderPreview"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, StepBodyIsSoleScrollOwnerAboveAStickyActionFooter) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto navigation = readFile(root / "direct_carve_preparation_navigation.cpp");

    const auto shellFlags =
        navigation.find("constexpr ImGuiWindowFlags kDirectCarveTaskSurfaceFlags");
    const auto surface = navigation.find("ImGui::BeginChild(\"##DirectCarveTaskSurface\"");
    const auto body = navigation.find("ImGui::BeginChild(\"##DirectCarveStepBody\"");
    const auto step = navigation.find("switch (m_currentStep)", body);
    const auto bodyEnd = navigation.find("ImGui::EndChild()", step);
    const auto footer = navigation.find("renderNavButtons()", bodyEnd);
    ASSERT_NE(shellFlags, std::string::npos);
    ASSERT_NE(surface, std::string::npos);
    ASSERT_NE(body, std::string::npos);
    ASSERT_NE(step, std::string::npos);
    ASSERT_NE(bodyEnd, std::string::npos);
    ASSERT_NE(footer, std::string::npos);
    EXPECT_NE(navigation.find("ImGuiWindowFlags_NoScrollbar", shellFlags), std::string::npos);
    EXPECT_NE(navigation.find("ImGuiWindowFlags_NoScrollWithMouse", shellFlags), std::string::npos);
    EXPECT_NE(navigation.find("kDirectCarveTaskSurfaceFlags", surface), std::string::npos);
    EXPECT_LT(body, step);
    EXPECT_LT(step, bodyEnd);
    EXPECT_LT(bodyEnd, footer);
    EXPECT_NE(navigation.find("showPreparationFooter", body), std::string::npos);
    EXPECT_NE(navigation.find("m_currentStep != Step::Running"), std::string::npos);
    const auto bodyCall = navigation.substr(body, step - body);
    EXPECT_NE(bodyCall.find("ImGuiWindowFlags_None"), std::string::npos);
    EXPECT_EQ(bodyCall.find("ImGuiWindowFlags_NoScrollbar"), std::string::npos);
    EXPECT_EQ(bodyCall.find("ImGuiWindowFlags_AlwaysVerticalScrollbar"), std::string::npos);
    EXPECT_NE(navigation.find("directCarveStickyFooterReserveHeight", surface), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ToolConfirmationRequiresRealGeometry) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto tool = readFile(root / "direct_carve_choose_tool_step.cpp");

    EXPECT_NE(tool.find("hasRealFinishingTool"), std::string::npos);
    EXPECT_NE(tool.find("isUsableDirectCarveTool(*finishing)"), std::string::npos);
    EXPECT_NE(tool.find("m_toolPlan.selectionComplete()"), std::string::npos);
    EXPECT_NE(tool.find("if (hasRealFinishingTool)"), std::string::npos);
    EXPECT_NE(tool.find("clearingComplete"), std::string::npos);
    const auto clearingGuard = tool.find("if (!clearingComplete)");
    ASSERT_NE(clearingGuard, std::string::npos);
    EXPECT_NE(tool.find("ImGui::BeginDisabled()", clearingGuard), std::string::npos);
    EXPECT_NE(tool.find("Tool plan reviewed"), std::string::npos);
    EXPECT_EQ(tool.find("installed and tightened"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ToolListShrinkWrapsAndOwnsLargeCatalogScroll) {
    const auto shortList =
        dw::chooseDirectCarveToolListLayout(600.0F, 5U, 24.0F, 28.0F, 180.0F, 1.0F);
    EXPECT_FLOAT_EQ(shortList.height, 150.0F);
    EXPECT_FALSE(shortList.scrolls);

    const auto longList =
        dw::chooseDirectCarveToolListLayout(600.0F, 100U, 24.0F, 28.0F, 180.0F, 1.0F);
    EXPECT_FLOAT_EQ(longList.height, 420.0F);
    EXPECT_TRUE(longList.scrolls);

    const auto noRows = dw::chooseDirectCarveToolListLayout(300.0F, 0U, 24.0F, 28.0F, 120.0F, 1.0F);
    EXPECT_FLOAT_EQ(noRows.height, 54.0F);
    EXPECT_FALSE(noRows.scrolls);
}

TEST(PrepareCarveUiArchitecture, ToolPickerUsesIndependentRolesAndNonOverlappingColumns) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto roleStep = readFile(root / "direct_carve_choose_tool_step.cpp");
    const auto picker = readFile(root / "direct_carve_tool_library_picker.cpp");
    const auto tool = roleStep + picker;

    EXPECT_NE(tool.find("Finishing / detail (required)##ToolRole"), std::string::npos);
    EXPECT_NE(tool.find("Clearing (optional)##ToolRole"), std::string::npos);
    EXPECT_NE(tool.find("Automatic##ClearingMode"), std::string::npos);
    EXPECT_NE(tool.find("Choose a tool##ClearingMode"), std::string::npos);
    EXPECT_NE(tool.find("No clearing pass##ClearingMode"), std::string::npos);
    EXPECT_NE(tool.find("m_toolPlan.selectTool(m_toolPickerRole, tool)"), std::string::npos);
    EXPECT_NE(tool.find("markToolPlanChanged()"), std::string::npos);
    EXPECT_EQ(tool.find("markToolpathSettingsChanged()"), std::string::npos);
    EXPECT_NE(tool.find("VtdbToolType::Radiused"), std::string::npos);

    EXPECT_NE(picker.find("##DirectCarveToolList"), std::string::npos);
    EXPECT_NE(picker.find("ImGui::BeginTable"), std::string::npos);
    EXPECT_NE(tool.find("TableSetupColumn(\"Tool\", ImGuiTableColumnFlags_WidthStretch"),
              std::string::npos);
    EXPECT_NE(tool.find("TableSetupColumn(\"Type\", ImGuiTableColumnFlags_WidthFixed"),
              std::string::npos);
    EXPECT_NE(tool.find("TableSetupColumn(\"Size\", ImGuiTableColumnFlags_WidthFixed"),
              std::string::npos);
    EXPECT_NE(tool.find("ImGuiSelectableFlags_SpanAllColumns"), std::string::npos);
    EXPECT_NE(tool.find("ImGuiSelectableFlags_Disabled"), std::string::npos);
    EXPECT_NE(tool.find("ImGuiHoveredFlags_AllowWhenDisabled"), std::string::npos);
    EXPECT_NE(tool.find("\"##SelectTool\""), std::string::npos);
    EXPECT_NE(tool.find("ImGui::TextUnformatted(name.c_str())"), std::string::npos);
    EXPECT_EQ(tool.find("ImGui::Selectable(name.c_str()"), std::string::npos);
    EXPECT_NE(tool.find("stableDirectCarveToolIdentity(tool)"), std::string::npos);
    EXPECT_NE(tool.find("ImGui::PushID(identity.c_str())"), std::string::npos);
    EXPECT_NE(tool.find("nameClipped"), std::string::npos);
    EXPECT_NE(tool.find("chooseDirectCarveToolListLayout"), std::string::npos);

    EXPECT_EQ(tool.find("char label[128]"), std::string::npos);
    EXPECT_EQ(tool.find("SmallButton(typeLabel)"), std::string::npos);
    EXPECT_EQ(tool.find("GetContentRegionAvail().x * 0.55f"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ToolPlanChangesInvalidateZeroAndOutlineGates) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto roleStep = readFile(root / "direct_carve_choose_tool_step.cpp");
    const auto picker = readFile(root / "direct_carve_tool_library_picker.cpp");
    const auto context = readFile(root / "direct_carve_preparation_context.cpp");

    EXPECT_NE(roleStep.find("markToolPlanChanged()"), std::string::npos);
    EXPECT_NE(picker.find("markToolPlanChanged()"), std::string::npos);

    const auto method = context.find("void DirectCarvePanel::markToolPlanChanged()");
    ASSERT_NE(method, std::string::npos);
    const auto methodEnd = context.find("\n}", method);
    ASSERT_NE(methodEnd, std::string::npos);
    const auto body = context.substr(method, methodEnd - method);
    EXPECT_NE(body.find("markToolpathSettingsChanged()"), std::string::npos);

    const auto invalidation = context.find("void DirectCarvePanel::markToolpathSettingsChanged()");
    ASSERT_NE(invalidation, std::string::npos);
    const auto invalidationEnd = context.find("\n}", invalidation);
    ASSERT_NE(invalidationEnd, std::string::npos);
    const auto invalidationBody = context.substr(invalidation, invalidationEnd - invalidation);
    EXPECT_NE(invalidationBody.find("m_zeroConfirmed = false"), std::string::npos);
    EXPECT_NE(invalidationBody.find("m_outlineCompleted = false"), std::string::npos);
    EXPECT_NE(invalidationBody.find("m_outlineSkipped = false"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ToolPlanPersistenceSeparatesIntentFromEffectiveResult) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto persistence = readFile(root / "direct_carve_project_sync.cpp");
    const auto resume = readFile(root / "direct_carve_operation_resume.cpp");

    EXPECT_NE(persistence.find("clearingToolModeKey"), std::string::npos);
    EXPECT_NE(persistence.find("selected_clearing_tool"), std::string::npos);
    EXPECT_NE(persistence.find("effective_clearing_tool"), std::string::npos);
    EXPECT_NE(persistence.find("m_toolPlan.selectionComplete()"), std::string::npos);
    EXPECT_EQ(persistence.find("snapshot[\"clearing_tool\"]"), std::string::npos);

    EXPECT_NE(resume.find("setup.selectedClearingTool"), std::string::npos);
    EXPECT_NE(resume.find("setup.effectiveClearingTool"), std::string::npos);
    EXPECT_NE(resume.find("setup.clearingToolMode"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, AutomaticRunSaveSuppressesOnlySuccessToast) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto gcode = readFile(root / "direct_carve_project_gcode.cpp");
    const auto run = readFile(root / "direct_carve_run_adapter.cpp");

    const auto guard = gcode.find("if (!requestedForRun)");
    const auto success = gcode.find("ToastType::Success, \"G-code Saved\"", guard);
    ASSERT_NE(guard, std::string::npos);
    ASSERT_NE(success, std::string::npos);
    EXPECT_LT(guard, success);
    EXPECT_NE(run.find("saveGCodeToProjectDirectory("), std::string::npos);
    EXPECT_NE(run.find("[this, pin](bool saved)"), std::string::npos);
    EXPECT_NE(gcode.find("ToastType::Error", guard), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, StepTrackerCompactsBeforeMeasuredLabelsOverlap) {
    EXPECT_FALSE(dw::directCarveStepIndicatorNeedsCompactLayout(1000.0f, 90.0f, 9, 8.0f));
    EXPECT_TRUE(dw::directCarveStepIndicatorNeedsCompactLayout(800.0f, 90.0f, 9, 8.0f));

    const auto indicator = readFile(fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels" /
                                    "direct_carve_step_indicator.cpp");
    EXPECT_NE(indicator.find("ImGui::CalcTextSize(stepLabel"), std::string::npos);
    EXPECT_NE(indicator.find("ImGui::TextWrapped(\"%s Step %d of %d"), std::string::npos);
    EXPECT_NE(indicator.find("markerNumber"), std::string::npos);
    EXPECT_NE(indicator.find("navigationStateLabel"), std::string::npos);
    EXPECT_NE(indicator.find("navigationStateIcon"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, WideTaskSurfaceStaysBoundedNearItsActions) {
    const auto scale100 = dw::chooseDirectCarveTaskLayout(3410.0F, 2000.0F, 13.0F);
    EXPECT_FLOAT_EQ(scale100.contentWidth, 936.0F);
    EXPECT_FLOAT_EQ(scale100.contentHeight, 494.0F);
    EXPECT_FLOAT_EQ(scale100.horizontalOffset, 1237.0F);

    const auto scale200 = dw::chooseDirectCarveTaskLayout(3410.0F, 2000.0F, 26.0F);
    EXPECT_FLOAT_EQ(scale200.contentWidth, 1872.0F);
    EXPECT_FLOAT_EQ(scale200.contentHeight, 988.0F);
    EXPECT_FLOAT_EQ(scale200.horizontalOffset, 769.0F);

    const auto compactHeight = dw::chooseDirectCarveTaskLayout(900.0F, 500.0F, 26.0F);
    EXPECT_FLOAT_EQ(compactHeight.contentWidth, 900.0F);
    EXPECT_FLOAT_EQ(compactHeight.contentHeight, 500.0F);
    EXPECT_FLOAT_EQ(compactHeight.horizontalOffset, 0.0F);
}

TEST(PrepareCarveUiArchitecture, FooterMeasuresLongPrimaryLabelAndStacksIfNeeded) {
    const auto oneRow = dw::chooseDirectCarveFooterLayout(
        900.0F, 26.0F, 42.0F, 8.0F, 8.0F, 8.0F, 55.0F, 300.0F, 75.0F);
    EXPECT_FALSE(oneRow.stacked);
    EXPECT_GE(oneRow.primaryWidth, 316.0F);
    EXPECT_FLOAT_EQ(oneRow.controlsHeight, 42.0F);

    const auto stacked = dw::chooseDirectCarveFooterLayout(
        480.0F, 26.0F, 42.0F, 8.0F, 8.0F, 8.0F, 55.0F, 300.0F, 75.0F);
    EXPECT_TRUE(stacked.stacked);
    EXPECT_FLOAT_EQ(stacked.primaryWidth, 480.0F);
    EXPECT_FLOAT_EQ(stacked.backWidth, 236.0F);
    EXPECT_FLOAT_EQ(stacked.cancelWidth, 236.0F);
    EXPECT_FLOAT_EQ(stacked.controlsHeight, 92.0F);
}

TEST(PrepareCarveUiArchitecture, StickyFooterReserveTracksDpiSpacingAndSeparatorSize) {
    EXPECT_FLOAT_EQ(dw::directCarveStickyFooterReserveHeight(42.0F, 0.0F, false, 6.0F, 1.0F),
                    61.0F);
    EXPECT_FLOAT_EQ(dw::directCarveStickyFooterReserveHeight(84.0F, 30.0F, true, 12.0F, 2.0F),
                    164.0F);

    // Invalid layout inputs cannot manufacture negative reserved space, and
    // ImGui separators retain their one-pixel minimum.
    EXPECT_FLOAT_EQ(dw::directCarveStickyFooterReserveHeight(-10.0F, -5.0F, true, -2.0F, 0.0F),
                    1.0F);
}

TEST(PrepareCarveUiArchitecture, ManualToolActionUsesCompactRowAtHighScale) {
    EXPECT_TRUE(dw::directCarveManualToolUsesCompactRow(760.0F, 26.0F));
    EXPECT_FALSE(dw::directCarveManualToolUsesCompactRow(700.0F, 26.0F));

    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto navigation = readFile(root / "direct_carve_preparation_navigation.cpp");
    const auto tool = readFile(root / "direct_carve_choose_tool_step.cpp");

    EXPECT_NE(navigation.find("##DirectCarveTaskSurface"), std::string::npos);
    EXPECT_NE(navigation.find("chooseDirectCarveTaskLayout"), std::string::npos);
    EXPECT_NE(navigation.find("chooseDirectCarveFooterLayout"), std::string::npos);
    EXPECT_NE(navigation.find("Continue to Machine Setup"), std::string::npos);
    EXPECT_NE(tool.find("##CompactManualTool"), std::string::npos);
    EXPECT_NE(tool.find("directCarveManualToolUsesCompactRow"), std::string::npos);
    EXPECT_NE(tool.find("Use This Tool"), std::string::npos);
    EXPECT_NE(tool.find("required novice action beside the primary diameter field"),
              std::string::npos);
}

TEST(PrepareCarveUiArchitecture, MachineAndRunViewsAreFocusedUnits) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto panel = readFile(root / "direct_carve_panel.cpp");

    struct ExtractedView {
        const char* file;
        const char* method;
    };
    for (const auto& view : {
             ExtractedView{"direct_carve_machine_check_step.cpp", "renderMachineCheck"},
             ExtractedView{"direct_carve_zero_confirm_step.cpp", "renderZeroConfirm"},
             ExtractedView{"direct_carve_outline_test_step.cpp", "renderOutlineTest"},
             ExtractedView{"direct_carve_review_run_step.cpp", "renderCommit"},
             ExtractedView{"direct_carve_active_run.cpp", "renderRunning"},
         }) {
        const auto source = readFile(root / view.file);
        EXPECT_LE(lineCount(root / view.file), 500U) << view.file;
        EXPECT_NE(source.find(view.method), std::string::npos) << view.file;
        EXPECT_EQ(panel.find(view.method), std::string::npos) << view.method;
    }
}

TEST(PrepareCarveUiArchitecture, ProtectedRunAdapterOwnsEveryMachineEffect) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto panels = root / "ui" / "panels";
    const auto adapter = readFile(panels / "direct_carve_run_adapter.cpp");
    const auto running = readFile(panels / "direct_carve_active_run.cpp");
    const auto navigation = readFile(panels / "direct_carve_preparation_navigation.cpp");
    const auto preparation =
        readFile(root / "modules" / "carve_preparation" / "prepare_carve_flow.h");
    const auto carveJob = readFile(root / "core" / "carve" / "carve_job.cpp");

    EXPECT_NE(adapter.find("RunPreflightSnapshot"), std::string::npos);
    EXPECT_NE(adapter.find("m_runCoordinator.dispatch(StartRun"), std::string::npos);
    EXPECT_NE(adapter.find("saveGCodeToProjectDirectory"), std::string::npos);
    EXPECT_NE(adapter.find("applyRunTransition"), std::string::npos);
    EXPECT_NE(adapter.find("PauseRun"), std::string::npos);
    EXPECT_NE(adapter.find("ResumeRun"), std::string::npos);
    EXPECT_NE(adapter.find("AbortRun"), std::string::npos);
    EXPECT_NE(adapter.find("CompleteRun"), std::string::npos);
    EXPECT_NE(adapter.find("FailRun"), std::string::npos);
    EXPECT_LE(lineCount(panels / "direct_carve_run_adapter.cpp"), 500U);

    for (const auto& forbidden :
         {"m_cnc->feedHold", "m_cnc->cycleStart", "m_cnc->softReset", "startStreaming"}) {
        EXPECT_EQ(running.find(forbidden), std::string::npos) << forbidden;
        EXPECT_EQ(navigation.find(forbidden), std::string::npos) << forbidden;
        EXPECT_EQ(preparation.find(forbidden), std::string::npos) << forbidden;
    }
    EXPECT_NE(running.find("requestRunPause"), std::string::npos);
    EXPECT_NE(running.find("requestRunResume"), std::string::npos);
    EXPECT_NE(running.find("requestRunAbort"), std::string::npos);
    EXPECT_NE(carveJob.find("controller->startStream(program)"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ExactGCodePreviewUsesViewportWithoutBecomingRunnable) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto panels = root / "ui" / "panels";
    const auto preview = readFile(panels / "direct_carve_gcode_preview.cpp");
    const auto previewStep = readFile(panels / "direct_carve_carve_preview_step.cpp");
    const auto persistence = readFile(panels / "direct_carve_project_gcode.cpp");
    const auto wiring = readFile(root / "app" / "application_wiring_cnc.cpp");
    const auto resume = readFile(root / "app" / "application_project_resume.cpp");
    const auto viewport = readFile(panels / "viewport_gcode_layer.cpp");

    EXPECT_NE(preview.find("carve::generateGcode"), std::string::npos);
    EXPECT_NE(preview.find("gcode::prepareDocument"), std::string::npos);
    EXPECT_EQ(preview.find("gcode::Parser"), std::string::npos);
    EXPECT_EQ(preview.find("GCodePanel"), std::string::npos);
    EXPECT_LE(lineCount(panels / "direct_carve_gcode_preview.cpp"), 250U);

    EXPECT_NE(previewStep.find("publishGCode3DPreview"), std::string::npos);
    EXPECT_NE(previewStep.find("Inspect in 3D"), std::string::npos);
    EXPECT_NE(persistence.find("file::writeTextAtomic"), std::string::npos);
    EXPECT_NE(persistence.find("loadPreparedFile"), std::string::npos);
    EXPECT_EQ(persistence.find("GCodeLoader"), std::string::npos);
    EXPECT_EQ(persistence.find("exportGcode"), std::string::npos);

    EXPECT_NE(wiring.find("ViewportGCodeSource::DirectCarvePreview"), std::string::npos);
    EXPECT_NE(wiring.find("clearGCodeProgramIfSource"), std::string::npos);
    EXPECT_NE(wiring.find("openWindow(\"viewport\")"), std::string::npos);
    EXPECT_NE(viewport.find("m_gcodeSource != source"), std::string::npos);

    const auto gcodeCase = resume.find("case ProjectOpenItemType::Gcode");
    ASSERT_NE(gcodeCase, std::string::npos);
    const auto operationCase = resume.find("case ProjectOpenItemType::Operation", gcodeCase);
    ASSERT_NE(operationCase, std::string::npos);
    const auto gcodeOpen = resume.substr(gcodeCase, operationCase - gcodeCase);
    EXPECT_NE(gcodeOpen.find("openWindow(\"viewport\")"), std::string::npos);
    EXPECT_EQ(gcodeOpen.find("openWindow(\"gcode_viewer\")"), std::string::npos);
}

TEST(PrepareCarveUiArchitecture, ExtractedStepsUsePinnedSnapshotGuidance) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto guidance = readFile(root / "direct_carve_step_guidance.cpp");

    EXPECT_NE(guidance.find("m_preparationFlow.snapshot()"), std::string::npos);
    EXPECT_NE(guidance.find("snapshot.pin()"), std::string::npos);
    EXPECT_NE(guidance.find("PreparationStepFacts"), std::string::npos);
    EXPECT_NE(guidance.find("buildPreparationStepPresentation"), std::string::npos);
    EXPECT_NE(guidance.find("TextWrapped(\"Next: %s\""), std::string::npos);
    EXPECT_EQ(guidance.find("TextDisabled(\"Next: %s\""), std::string::npos);

    const auto preview = readFile(root / "direct_carve_carve_preview_step.cpp");
    EXPECT_NE(preview.find("TextWrapped(\"No roughing suggestion: %s\""), std::string::npos);
    EXPECT_EQ(preview.find("TextDisabled(\"No roughing suggestion: %s\""), std::string::npos);

    for (const auto* step : {"direct_carve_design_size_step.cpp",
                             "direct_carve_material_blank_step.cpp",
                             "direct_carve_choose_tool_step.cpp",
                             "direct_carve_carve_preview_step.cpp"}) {
        const auto source = readFile(root / step);
        EXPECT_NE(source.find("renderPreparationStepGuidance"), std::string::npos) << step;
    }
}
