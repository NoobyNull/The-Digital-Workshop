#include "preparation_step_guidance.h"

#include <cstddef>
#include <utility>

namespace dw::carve_preparation {
namespace {

std::size_t stageIndex(PreparationStageId stage) noexcept {
    switch (stage) {
    case PreparationStageId::DesignAndSize: return 0;
    case PreparationStageId::MaterialAndBlank: return 1;
    case PreparationStageId::ChooseTool: return 2;
    case PreparationStageId::CarvePreview: return 3;
    }
    return 0;
}

const char* stageTitle(PreparationStageId stage) noexcept {
    switch (stage) {
    case PreparationStageId::DesignAndSize: return "Design & Size";
    case PreparationStageId::MaterialAndBlank: return "Material & Blank";
    case PreparationStageId::ChooseTool: return "Choose Tool";
    case PreparationStageId::CarvePreview: return "Carve Preview";
    }
    return "Prepare Carve";
}

const char* whyItMatters(PreparationStageId stage) noexcept {
    switch (stage) {
    case PreparationStageId::DesignAndSize:
        return "Confirm the design size and machine fit before planning the cut.";
    case PreparationStageId::MaterialAndBlank:
        return "Material and blank size come before tool choice because safe cutting "
               "settings depend on what is being cut and how much room is available.";
    case PreparationStageId::ChooseTool:
        return "Choose a tool after the material so the recommendation can match the "
               "material, blank, and design detail.";
    case PreparationStageId::CarvePreview:
        return "Generate and inspect the toolpath before moving to the machine.";
    }
    return "Complete this preparation step before continuing.";
}

const char* guidanceForBlocker(PreparationBlockerCode blocker) noexcept {
    switch (blocker) {
    case PreparationBlockerCode::PreviousStageIncomplete:
        return "Finish the previous step before continuing here.";
    case PreparationBlockerCode::DesignLoadUnknown:
        return "Design details are still loading. Wait for them before continuing.";
    case PreparationBlockerCode::DesignNotLoaded:
        return "Choose and load a design to begin.";
    case PreparationBlockerCode::MachineFitUnknown:
        return "Machine fit has not been checked yet. Check the fitted size before continuing.";
    case PreparationBlockerCode::DesignDoesNotFitMachine:
        return "Reduce or reposition the design until it fits the machine travel.";
    case PreparationBlockerCode::MaterialSelectionUnknown:
        return "Material details are still loading. Wait or choose the material again.";
    case PreparationBlockerCode::MaterialNotSelected:
        return "Choose the material you will actually carve.";
    case PreparationBlockerCode::BlankSpecificationUnknown:
        return "Blank dimensions have not been checked yet. Enter the measured blank size.";
    case PreparationBlockerCode::BlankNotSpecified:
        return "Measure the blank and enter its width, length, and thickness.";
    case PreparationBlockerCode::BlankFitUnknown:
        return "Blank fit has not been checked yet. Confirm the design fits inside it.";
    case PreparationBlockerCode::DesignDoesNotFitBlank:
        return "Use a larger blank or reduce the design until it fits with a safe margin.";
    case PreparationBlockerCode::ToolSelectionUnknown:
        return "Tool information is still loading. Wait or choose the tool again.";
    case PreparationBlockerCode::ToolNotSelected:
        return "Review the recommendation, then choose the tool installed for this carve.";
    case PreparationBlockerCode::ToolSetupUnknown:
        return "Tool settings have not been checked yet. Review them before continuing.";
    case PreparationBlockerCode::ToolSetupNotConfirmed:
        return "Confirm the tool and cutting settings you intend to use.";
    case PreparationBlockerCode::PreviewGenerationUnknown:
        return "Preview status is still loading. Wait or generate the preview again.";
    case PreparationBlockerCode::PreviewNotGenerated:
        return "Generate the carve preview, then inspect the complete toolpath.";
    case PreparationBlockerCode::PreviewFreshnessUnknown:
        return "Preview freshness has not been checked yet. Regenerate it if setup changed.";
    case PreparationBlockerCode::PreviewStale:
        return "Setup changed after the last preview. Generate a fresh preview.";
    }
    return "Review the missing setup information before continuing.";
}

const char* completeGuidance(PreparationStageId stage) noexcept {
    switch (stage) {
    case PreparationStageId::DesignAndSize:
        return "Next, choose the material and enter the measured blank size.";
    case PreparationStageId::MaterialAndBlank:
        return "Next, choose a tool; recommendations can now use the material and blank.";
    case PreparationStageId::ChooseTool:
        return "Next, generate the carve preview and inspect every cut.";
    case PreparationStageId::CarvePreview:
        return "The preview is ready. Continue to Machine Setup when it looks correct.";
    }
    return "Continue to the next preparation step.";
}

std::string nextGuidance(const PreparationStageProjection& stage) {
    if (!stage.blockers.empty()) return guidanceForBlocker(stage.blockers.front().code);
    if (stage.state == PreparationStageState::Complete)
        return completeGuidance(stage.id);
    return "Review this step and complete the required information.";
}

PreparationPrimaryActionPresentation primaryAction(
    const PreparationStageProjection& stage,
    bool enabled) {
    if (!enabled) return {};

    PreparationPrimaryActionPresentation action;
    switch (stage.id) {
    case PreparationStageId::DesignAndSize:
        action.kind = PreparationPrimaryActionKind::Continue;
        action.label = "Continue to Material & Blank";
        action.available = stage.state == PreparationStageState::Complete;
        break;
    case PreparationStageId::MaterialAndBlank:
        action.kind = PreparationPrimaryActionKind::Continue;
        action.label = "Continue to Choose Tool";
        action.available = stage.state == PreparationStageState::Complete;
        break;
    case PreparationStageId::ChooseTool:
        action.kind = PreparationPrimaryActionKind::Continue;
        action.label = "Continue to Carve Preview";
        action.available = stage.state == PreparationStageState::Complete;
        break;
    case PreparationStageId::CarvePreview:
        action.available = stage.state != PreparationStageState::Locked;
        if (stage.state == PreparationStageState::Complete) {
            action.kind = PreparationPrimaryActionKind::Continue;
            action.label = "Continue to Machine Setup";
        } else {
            action.kind = PreparationPrimaryActionKind::GeneratePreview;
            action.label = "Generate Carve Preview";
        }
        break;
    }
    return action;
}

void addRecommendation(const PreparationStepFacts& facts,
                       PreparationStepPresentation& presentation) {
    if (facts.recommendation()) {
        presentation.recommendationLabel = facts.recommendation()->label;
        presentation.recommendationRationale = facts.recommendation()->reason.empty()
                                                   ? "Review this recommendation before selecting it."
                                                   : facts.recommendation()->reason;
        return;
    }

    if (facts.stage().id != PreparationStageId::ChooseTool) return;
    if (facts.stage().state == PreparationStageState::Locked) {
        presentation.recommendationRationale =
            "Finish Material & Blank first so tool guidance can account for the material "
            "and available blank.";
    } else {
        presentation.recommendationRationale =
            "No tool recommendation is available yet. Review the material and design "
            "detail before choosing a tool.";
    }
}

} // namespace

bool PreparationAdvancedState::expanded(PreparationStageId stage) const noexcept {
    return m_expanded[stageIndex(stage)];
}

PreparationAdvancedState PreparationAdvancedState::withExpansion(
    PreparationStageId stage,
    bool expandedValue) const noexcept {
    auto next = *this;
    next.m_expanded[stageIndex(stage)] = expandedValue;
    return next;
}

PreparationStepFacts::PreparationStepFacts(
    PrepareCarvePin pin,
    PreparationStageProjection stage,
    PreparationAdvancedState advanced,
    std::optional<PreparationRecommendationFact> recommendation)
    : m_pin(std::move(pin)),
      m_stage(std::move(stage)),
      m_advanced(advanced),
      m_recommendation(std::move(recommendation)) {}

const PrepareCarvePin& PreparationStepFacts::pin() const noexcept {
    return m_pin;
}

const PreparationStageProjection& PreparationStepFacts::stage() const noexcept {
    return m_stage;
}

const PreparationAdvancedState& PreparationStepFacts::advanced() const noexcept {
    return m_advanced;
}

const std::optional<PreparationRecommendationFact>&
PreparationStepFacts::recommendation() const noexcept {
    return m_recommendation;
}

PreparationStepPresentation buildPreparationStepPresentation(
    const PreparationStepFacts& facts,
    PreparationStepGuidanceOptions options) {
    PreparationStepPresentation presentation;
    presentation.stage = facts.stage().id;
    presentation.title = stageTitle(facts.stage().id);
    presentation.whyItMatters = whyItMatters(facts.stage().id);
    presentation.nextGuidance = nextGuidance(facts.stage());
    addRecommendation(facts, presentation);
    presentation.primaryAction = primaryAction(facts.stage(), options.enabled);
    presentation.advanced.available = options.enabled && options.advancedAvailable;
    presentation.advanced.expanded = facts.advanced().expanded(facts.stage().id);
    presentation.advanced.label = presentation.advanced.expanded ? "Hide Advanced"
                                                                 : "Show Advanced";
    return presentation;
}

} // namespace dw::carve_preparation
