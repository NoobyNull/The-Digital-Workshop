// Direct Carve preparation navigation and presentation policy.
//
// Project-pinned preparation is governed by PrepareCarveFlow. Standalone
// Advanced use deliberately keeps the legacy freely navigable wizard.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>

#include <imgui.h>

#include "core/config/config.h"
#include "ui/panels/direct_carve_layout_policy.h"
#include "ui/ui_colors.h"

namespace dw {
namespace {

using carve_preparation::PreparationStageId;
using carve_preparation::PreparationStageState;

constexpr auto& kYellow = colors::kWarning;
constexpr const char* kLegacySkipNote =
    "Gate incomplete; skipping will not unlock final carve.";
constexpr ImGuiWindowFlags kDirectCarveTaskSurfaceFlags =
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

const char* primaryNavigationLabel(bool commit,
                                   bool pinnedPlanning,
                                   bool preview,
                                   bool currentSatisfied) noexcept {
    if (commit) return "Start Carving";
    if (pinnedPlanning)
        return preview ? "Continue to Machine Setup" : "Continue";
    return currentSatisfied ? "Next" : "Skip";
}

u32 activePreparationLimitPins(const MachineStatus& status) noexcept {
    return status.inputPins &
        (cnc::PIN_X_LIMIT | cnc::PIN_Y_LIMIT | cnc::PIN_Z_LIMIT);
}

} // namespace

const char* DirectCarvePanel::stepLabel(Step step) {
    switch (step) {
    case Step::ModelFit:      return "Design";
    case Step::MaterialSetup: return "Material";
    case Step::ToolSelect:    return "Tool";
    case Step::Preview:       return "Preview";
    case Step::MachineCheck:  return "Machine 1/3";
    case Step::ZeroConfirm:   return "Machine 2/3";
    case Step::OutlineTest:   return "Machine 3/3";
    case Step::Commit:        return "Review & Run";
    case Step::Running:       return "Run CNC";
    }
    return "???";
}

std::optional<PreparationStageId>
DirectCarvePanel::preparationStage(Step step) noexcept {
    switch (step) {
    case Step::ModelFit:      return PreparationStageId::DesignAndSize;
    case Step::MaterialSetup: return PreparationStageId::MaterialAndBlank;
    case Step::ToolSelect:    return PreparationStageId::ChooseTool;
    case Step::Preview:       return PreparationStageId::CarvePreview;
    default:                  return std::nullopt;
    }
}

DirectCarvePanel::Step
DirectCarvePanel::preparationStep(PreparationStageId stage) noexcept {
    switch (stage) {
    case PreparationStageId::DesignAndSize:    return Step::ModelFit;
    case PreparationStageId::MaterialAndBlank: return Step::MaterialSetup;
    case PreparationStageId::ChooseTool:       return Step::ToolSelect;
    case PreparationStageId::CarvePreview:     return Step::Preview;
    }
    return Step::ModelFit;
}

carve::DirectCarveWorkflowStep DirectCarvePanel::workflowStep(Step step) const {
    switch (step) {
    case Step::ModelFit:      return carve::DirectCarveWorkflowStep::Model;
    case Step::MaterialSetup: return carve::DirectCarveWorkflowStep::Material;
    case Step::ToolSelect:    return carve::DirectCarveWorkflowStep::Tool;
    case Step::Preview:       return carve::DirectCarveWorkflowStep::Preview;
    case Step::MachineCheck:  return carve::DirectCarveWorkflowStep::Machine;
    case Step::ZeroConfirm:   return carve::DirectCarveWorkflowStep::Zero;
    case Step::OutlineTest:   return carve::DirectCarveWorkflowStep::Outline;
    case Step::Commit:        return carve::DirectCarveWorkflowStep::Confirm;
    case Step::Running:       return carve::DirectCarveWorkflowStep::Running;
    }
    return carve::DirectCarveWorkflowStep::Running;
}

carve::DirectCarveWorkflowState DirectCarvePanel::workflowState() const {
    carve::DirectCarveWorkflowState state;
    state.modelLoaded = m_modelLoaded;

    if (m_modelLoaded) {
        const auto& profile = Config::instance().getActiveMachineProfile();
        carve::ModelFitter fitter = m_fitter;
        fitter.setStock(m_stock);
        fitter.setMachineTravel(profile.maxTravelX,
                                profile.maxTravelY,
                                profile.maxTravelZ);
        const auto fit = fitter.fit(m_fitParams);
        state.modelFitsBlank = fit.fitsStock;
        state.modelFitsMachine = fit.fitsMachine;
    }

    state.finishingToolSelected = m_toolPlan.finishingIntent().has_value();
    state.toolSetupConfirmed = m_toolSetupConfirmed;
    state.materialSelected = m_materialSelected;
    state.toolpathGenerated = m_toolpathGenerated;
    state.toolpathFresh = m_toolpathGenerated &&
                          m_generatedAtVersion == m_settingsVersion;

    const auto& profile = Config::instance().getActiveMachineProfile();
    state.machineConnected = m_cncConnected;
    state.machineIdle = m_machineStatus.state == MachineState::Idle;
    state.machineAlarmClear = m_machineStatus.state != MachineState::Alarm &&
                              m_machineStatus.state != MachineState::Unknown;
    state.machineProfileConfigured = profile.maxTravelX > 0.0f &&
                                     profile.maxTravelY > 0.0f &&
                                     profile.maxTravelZ > 0.0f;
    state.machineHomed = m_homingVerified;
    state.homingSkipped = m_homingSkipped;
    state.limitSwitchesClear = activePreparationLimitPins(m_machineStatus) == 0;
    state.safeZVerified = m_safeZConfirmed;
    state.zeroVerified = m_zeroConfirmed;
    state.outlineCompleted = m_outlineCompleted;
    state.outlineSkipped = m_outlineSkipped;
    state.finalConfirmed = m_commitConfirmed &&
        m_commitConfirmedSettingsVersion == m_settingsVersion &&
        m_commitConfirmedToolpathVersion == m_generatedAtVersion;
    return state;
}

bool DirectCarvePanel::isStepSatisfied(Step step) const {
    if (pinnedPreparationActive()) {
        if (const auto stage = preparationStage(step)) {
            return m_preparationFlow.snapshot().stage(*stage).state ==
                   PreparationStageState::Complete;
        }
    }
    return carve::isDirectCarveStepComplete(workflowStep(step), workflowState());
}

bool DirectCarvePanel::canStartCarve() const {
    if (pinnedPreparationActive() &&
        m_preparationFlow.snapshot().stage(PreparationStageId::CarvePreview).state !=
            PreparationStageState::Complete) {
        return false;
    }
    return m_stockSecuredConfirmed && m_runEffectExecutor &&
           carve::isDirectCarveReadyToRun(workflowState());
}

void DirectCarvePanel::render() {
    if (!m_open) return;
    applyMinSize(40.0f, 25.0f);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) { ImGui::End(); return; }

    refreshPinnedPreparation();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const auto taskLayout = chooseDirectCarveTaskLayout(
        available.x, available.y, ImGui::GetFontSize());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         taskLayout.horizontalOffset);
    ImGui::BeginChild("##DirectCarveTaskSurface",
                      {taskLayout.contentWidth, taskLayout.contentHeight},
                      ImGuiChildFlags_None,
                      kDirectCarveTaskSurfaceFlags);

    renderPreparationContext();
    renderStepIndicator();
    ImGui::Separator();
    ImGui::Spacing();

    const bool showPreparationFooter = m_currentStep != Step::Running;
    float footerHeight = 0.0F;
    if (showPreparationFooter) {
        const bool commit = m_currentStep == Step::Commit;
        const bool pinnedPlanning = pinnedPreparationActive() &&
                                    preparationStage(m_currentStep).has_value();
        const bool currentSatisfied = isStepSatisfied(m_currentStep);
        const char* primaryLabel = primaryNavigationLabel(
            commit, pinnedPlanning, m_currentStep == Step::Preview,
            currentSatisfied);
        const auto& style = ImGui::GetStyle();
        const auto footer = chooseDirectCarveFooterLayout(
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFontSize(),
            ImGui::GetFrameHeight(),
            style.ItemSpacing.x,
            style.ItemSpacing.y,
            style.FramePadding.x,
            ImGui::CalcTextSize("Back").x,
            ImGui::CalcTextSize(primaryLabel).x,
            ImGui::CalcTextSize("Cancel").x);
        const bool legacyNote = !pinnedPlanning && !commit &&
                                !currentSatisfied;
        const float noteTextHeight = legacyNote
            ? ImGui::CalcTextSize(kLegacySkipNote, nullptr, false,
                                  ImGui::GetContentRegionAvail().x).y
            : 0.0F;
        footerHeight = directCarveStickyFooterReserveHeight(
            footer.controlsHeight,
            noteTextHeight,
            legacyNote,
            style.ItemSpacing.y,
            style.SeparatorSize);
    }
    if (ImGui::BeginChild("##DirectCarveStepBody",
                          ImVec2(0.0f, -footerHeight),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_None)) {
        switch (m_currentStep) {
        case Step::ModelFit:      renderModelFit();      break;
        case Step::MaterialSetup: renderMaterialSetup(); break;
        case Step::ToolSelect:    renderToolSelect();    break;
        case Step::Preview:       renderPreview();       break;
        case Step::MachineCheck:  renderMachineCheck();  break;
        case Step::ZeroConfirm:   renderZeroConfirm();   break;
        case Step::OutlineTest:   renderOutlineTest();   break;
        case Step::Commit:        renderCommit();        break;
        case Step::Running:       renderRunning();       break;
        }
    }
    ImGui::EndChild();

    if (showPreparationFooter) {
        refreshPinnedPreparation();
        ImGui::Separator();
        ImGui::Spacing();
        renderNavButtons();
    }
    ImGui::EndChild();
    ImGui::End();
}

void DirectCarvePanel::navigateToStep(Step target) {
    const int targetIdx = static_cast<int>(target);
    const int currentIdx = static_cast<int>(m_currentStep);

    if (pinnedPreparationActive()) {
        refreshPinnedPreparation();
        if (const auto stage = preparationStage(target)) {
            const auto transition = m_preparationFlow.dispatch(
                carve_preparation::OpenPreparationStage{*stage});
            if (transition.status != carve_preparation::PrepareCarveTransitionStatus::Applied &&
                transition.status != carve_preparation::PrepareCarveTransitionStatus::Unchanged) {
                return;
            }
            if (targetIdx > currentIdx &&
                (m_currentStep == Step::ModelFit ||
                 m_currentStep == Step::MaterialSetup)) {
                syncSetupToOptimizerAndProject();
            }
            m_currentStep = preparationStep(transition.snapshot.activeStage);
            m_maxStepVisited = std::max(m_maxStepVisited,
                                        static_cast<int>(m_currentStep));
            return;
        }

        const auto previewComplete =
            m_preparationFlow.snapshot().stage(PreparationStageId::CarvePreview).state ==
            PreparationStageState::Complete;
        if (!previewComplete) return;
    }

    if (!carve::canNavigateDirectCarveStep(targetIdx, m_maxStepVisited, STEP_COUNT))
        return;
    if (targetIdx > currentIdx &&
        (m_currentStep == Step::ModelFit || m_currentStep == Step::MaterialSetup)) {
        syncSetupToOptimizerAndProject();
    }
    m_currentStep = target;
    m_maxStepVisited = std::max(m_maxStepVisited, targetIdx);
}

void DirectCarvePanel::renderNavButtons() {
    refreshPinnedPreparation();
    const bool first = m_currentStep == Step::ModelFit;
    const bool running = m_currentStep == Step::Running;
    const bool commit = m_currentStep == Step::Commit;
    const bool pinnedPlanning = pinnedPreparationActive() &&
                                preparationStage(m_currentStep).has_value();

    const bool mayContinue = canAdvance();
    const bool currentSatisfied = isStepSatisfied(m_currentStep);
    const char* nextLabel = primaryNavigationLabel(
        commit, pinnedPlanning, m_currentStep == Step::Preview,
        currentSatisfied);
    const auto& style = ImGui::GetStyle();
    const auto layout = chooseDirectCarveFooterLayout(
        ImGui::GetContentRegionAvail().x,
        ImGui::GetFontSize(),
        ImGui::GetFrameHeight(),
        style.ItemSpacing.x,
        style.ItemSpacing.y,
        style.FramePadding.x,
        ImGui::CalcTextSize("Back").x,
        ImGui::CalcTextSize(nextLabel).x,
        ImGui::CalcTextSize("Cancel").x);

    const bool showLegacySkipNote = !pinnedPlanning && !commit &&
                                    !currentSatisfied && !running;

    const auto renderBack = [&]() {
        if (first || running) ImGui::BeginDisabled();
        if (ImGui::Button("Back", {layout.backWidth, 0})) retreatStep();
        if (first || running) ImGui::EndDisabled();
    };
    const auto renderPrimary = [&]() {
        if (!mayContinue) ImGui::BeginDisabled();
        if (ImGui::Button(nextLabel, {layout.primaryWidth, 0})) advanceStep();
        if (!mayContinue) ImGui::EndDisabled();
    };
    const auto renderCancel = [&]() {
        if (running) ImGui::BeginDisabled();
        if (!ImGui::Button("Cancel", {layout.cancelWidth, 0})) {
            if (running) ImGui::EndDisabled();
            return;
        }
        if (m_preparationPin) {
            clearProjectContext();
        } else {
            clearGCode3DPreview();
            m_currentStep = Step::ModelFit;
            m_safeZConfirmed = false;
            m_homingVerified = false;
            m_homingSkipped = false;
            m_toolPlan = {};
            m_toolPickerRole = carve::DirectCarveToolPickerRole::Finishing;
            m_toolSetupConfirmed = false;
            m_materialSelected = false;
            m_toolpathGenerated = false;
            m_settingsVersion = 0;
            m_generatedAtVersion = -1;
            m_outlineCompleted = false;
            m_outlineSkipped = false;
            m_outlineRunning = false;
            m_zeroConfirmed = false;
            clearFinalConfirmation();
            m_maxStepVisited = 0;
        }
        if (running) ImGui::EndDisabled();
    };

    if (layout.stacked) {
        renderPrimary();
        renderBack();
        ImGui::SameLine();
        renderCancel();
    } else {
        renderBack();
        ImGui::SameLine();
        renderPrimary();
        ImGui::SameLine();
        renderCancel();
    }
    if (showLegacySkipNote) {
        ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
        ImGui::TextWrapped("%s", kLegacySkipNote);
        ImGui::PopStyleColor();
    }
}

bool DirectCarvePanel::canAdvance() {
    if (m_currentStep == Step::Running) return false;
    if (m_currentStep == Step::Commit) return canStartCarve();
    if (pinnedPreparationActive()) {
        if (const auto stage = preparationStage(m_currentStep)) {
            return m_preparationFlow.snapshot().stage(*stage).state ==
                   PreparationStageState::Complete;
        }
    }
    return true;
}

void DirectCarvePanel::advanceStep() {
    if (m_currentStep == Step::Commit) {
        requestRunStart();
        return;
    }
    if (pinnedPreparationActive()) {
        refreshPinnedPreparation();
        if (const auto stage = preparationStage(m_currentStep)) {
            const auto oldStep = m_currentStep;
            const auto transition = m_preparationFlow.dispatch(
                carve_preparation::ContinuePreparation{});
            if (transition.status == carve_preparation::PrepareCarveTransitionStatus::Rejected ||
                transition.status == carve_preparation::PrepareCarveTransitionStatus::Disabled) {
                return;
            }
            if (*stage == PreparationStageId::CarvePreview) {
                if (transition.status !=
                        carve_preparation::PrepareCarveTransitionStatus::EffectIssued ||
                    !transition.effect ||
                    !std::holds_alternative<carve_preparation::PreparationReady>(
                        *transition.effect)) {
                    return;
                }
                navigateToStep(Step::MachineCheck);
            } else {
                if (transition.status !=
                    carve_preparation::PrepareCarveTransitionStatus::Applied) {
                    return;
                }
                m_currentStep = preparationStep(transition.snapshot.activeStage);
                if (oldStep == Step::ModelFit || oldStep == Step::MaterialSetup)
                    syncSetupToOptimizerAndProject();
                m_maxStepVisited = std::max(m_maxStepVisited,
                                            static_cast<int>(m_currentStep));
            }
            return;
        }
    }

    const int index = static_cast<int>(m_currentStep);
    if (index < STEP_COUNT - 1)
        navigateToStep(static_cast<Step>(index + 1));
}

void DirectCarvePanel::retreatStep() {
    const int index = static_cast<int>(m_currentStep);
    if (index <= 0) return;

    if (pinnedPreparationActive()) {
        if (m_currentStep == Step::MachineCheck) {
            navigateToStep(Step::Preview);
            return;
        }
        if (preparationStage(m_currentStep)) {
            navigateToStep(static_cast<Step>(index - 1));
            return;
        }
    }
    m_currentStep = static_cast<Step>(index - 1);
}

} // namespace dw
