// Live Direct Carve state adapter for the pure preparation-flow module.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>

#include "core/config/config.h"

namespace dw {
namespace {

using carve_preparation::PreparationEvidence;
using carve_preparation::PreparationReadinessSnapshot;
using carve_preparation::PreparationStageId;
using carve_preparation::PreparationStageState;

PreparationEvidence knownEvidence(bool value) noexcept {
    return value ? PreparationEvidence::Satisfied : PreparationEvidence::Unsatisfied;
}

bool samePreparationFacts(const PreparationReadinessSnapshot& lhs,
                          const PreparationReadinessSnapshot& rhs) noexcept {
    return lhs.modelLoaded == rhs.modelLoaded &&
           lhs.modelFitsMachine == rhs.modelFitsMachine &&
           lhs.materialSelected == rhs.materialSelected &&
           lhs.blankSpecified == rhs.blankSpecified &&
           lhs.modelFitsBlank == rhs.modelFitsBlank &&
           lhs.finishingToolSelected == rhs.finishingToolSelected &&
           lhs.toolSetupConfirmed == rhs.toolSetupConfirmed &&
           lhs.toolpathGenerated == rhs.toolpathGenerated &&
           lhs.toolpathFresh == rhs.toolpathFresh &&
           lhs.hasUnsavedChanges == rhs.hasUnsavedChanges;
}

} // namespace

std::optional<PreparationReadinessSnapshot>
DirectCarvePanel::preparationReadinessSnapshot() const {
    if (!m_preparationPin) return std::nullopt;

    PreparationReadinessSnapshot facts(
        *m_preparationPin,
        carve_preparation::PreparationEditRevision{m_preparationEditRevision});
    facts.modelLoaded = knownEvidence(m_modelLoaded);
    facts.modelFitsMachine = PreparationEvidence::Unknown;
    facts.materialSelected = knownEvidence(m_materialSelected);
    const bool blankSpecified = m_stock.width > 0.0f &&
                                m_stock.height > 0.0f &&
                                m_stock.thickness > 0.0f;
    facts.blankSpecified = knownEvidence(blankSpecified);
    facts.modelFitsBlank = PreparationEvidence::Unknown;
    facts.finishingToolSelected = knownEvidence(
        m_toolPlan.finishingIntent().has_value());
    facts.toolSetupConfirmed = knownEvidence(m_toolSetupConfirmed);
    facts.toolpathGenerated = knownEvidence(m_toolpathGenerated);
    facts.toolpathFresh = knownEvidence(
        m_toolpathGenerated && m_generatedAtVersion == m_settingsVersion);
    facts.hasUnsavedChanges = m_preparationDirty;

    if (m_modelLoaded) {
        const auto& profile = Config::instance().getActiveMachineProfile();
        carve::ModelFitter fitter = m_fitter;
        fitter.setStock(m_stock);
        fitter.setMachineTravel(profile.maxTravelX,
                                profile.maxTravelY,
                                profile.maxTravelZ);
        const auto fit = fitter.fit(m_fitParams);
        facts.modelFitsMachine = knownEvidence(fit.fitsMachine);
        if (blankSpecified)
            facts.modelFitsBlank = knownEvidence(fit.fitsStock);
    }
    return facts;
}

std::optional<DirectCarveProjectPlanSnapshot>
DirectCarvePanel::projectPlanSnapshot() const {
    if (!m_preparationPin || !pinnedPreparationActive())
        return std::nullopt;
    return DirectCarveProjectPlanSnapshot{
        *m_preparationPin,
        workflowState(),
        m_stock.width > 0.0F && m_stock.height > 0.0F &&
            m_stock.thickness > 0.0F,
    };
}

bool DirectCarvePanel::pinnedPreparationActive() const noexcept {
    const auto* activePin = m_preparationFlow.snapshot().pin();
    return m_preparationPin && m_preparationFlow.snapshot().active && activePin &&
           *activePin == *m_preparationPin;
}

void DirectCarvePanel::beginPinnedPreparation() {
    if (!m_preparationPin || m_preparationFlow.snapshot().active) return;
    m_preparationEditRevision = 1;
    m_lastPreparationSettingsVersion = m_settingsVersion;
    const auto facts = preparationReadinessSnapshot();
    if (!facts) return;
    const auto transition = m_preparationFlow.dispatch(
        carve_preparation::BeginPreparation{*facts});
    if (transition.status == carve_preparation::PrepareCarveTransitionStatus::Applied) {
        m_currentStep = preparationStep(transition.snapshot.activeStage);
        m_maxStepVisited = std::max(m_maxStepVisited,
                                    static_cast<int>(m_currentStep));
    }
}

void DirectCarvePanel::refreshPinnedPreparation() {
    if (!m_preparationPin) return;
    if (!m_preparationFlow.snapshot().active) {
        beginPinnedPreparation();
        return;
    }
    if (!pinnedPreparationActive()) return;

    auto facts = preparationReadinessSnapshot();
    if (!facts) return;
    const auto& current = m_preparationFlow.snapshot().readiness;
    if ((current && !samePreparationFacts(*current, *facts)) ||
        m_lastPreparationSettingsVersion != m_settingsVersion) {
        ++m_preparationEditRevision;
        m_lastPreparationSettingsVersion = m_settingsVersion;
        facts = preparationReadinessSnapshot();
    }
    if (!facts) return;

    const auto transition = m_preparationFlow.dispatch(
        carve_preparation::RefreshPreparation{*facts});
    const auto& snapshot = transition.snapshot;
    const auto previewState = snapshot.stage(PreparationStageId::CarvePreview).state;
    if (preparationStage(m_currentStep)) {
        m_currentStep = preparationStep(snapshot.activeStage);
    } else if (previewState != PreparationStageState::Complete &&
               !hasActiveMachineAction()) {
        m_currentStep = preparationStep(snapshot.activeStage);
    }
}

bool DirectCarvePanel::requestPinnedPreviewGeneration() {
    if (!m_preparationPin) return true;
    refreshPinnedPreparation();
    if (!pinnedPreparationActive()) return false;

    const auto transition = m_preparationFlow.dispatch(
        carve_preparation::GeneratePreparationPreview{});
    if (transition.status !=
            carve_preparation::PrepareCarveTransitionStatus::EffectIssued ||
        !transition.effect ||
        !std::holds_alternative<carve_preparation::PreparationPreviewRequest>(
            *transition.effect)) {
        return false;
    }
    const auto& request =
        std::get<carve_preparation::PreparationPreviewRequest>(*transition.effect);
    return request.readiness.pin() == *m_preparationPin &&
           transition.snapshot.pendingPreviewRevision ==
               request.readiness.editRevision();
}

void DirectCarvePanel::completePinnedPreviewGeneration(bool generated) {
    if (!m_preparationPin || !pinnedPreparationActive()) return;
    const auto requestRevision = m_preparationFlow.snapshot().pendingPreviewRevision;
    if (!requestRevision) return;

    if (generated) setPreparationDirty(true);
    const auto pin = *m_preparationPin;
    (void)m_preparationFlow.dispatch(
        carve_preparation::CompletePreparationPreview{
            pin, *requestRevision, generated});
    refreshPinnedPreparation();
}

std::optional<i64> DirectCarvePanel::syncOperationOpenItem() {
    if (m_preparationPin) {
        syncToolpathRapidRateFromProfile();
        refreshPinnedPreparation();
        if (!pinnedPreparationActive()) return std::nullopt;
    }

    std::optional<carve_preparation::PreparationEditRevision> requestRevision;
    std::optional<carve_preparation::PrepareCarvePin> requestPin;
    if (m_preparationPin && m_preparationDirty) {
        const auto request = m_preparationFlow.dispatch(
            carve_preparation::SavePreparation{});
        if (request.status !=
                carve_preparation::PrepareCarveTransitionStatus::EffectIssued ||
            !request.effect ||
            !std::holds_alternative<carve_preparation::PreparationSaveRequest>(
                *request.effect)) {
            return std::nullopt;
        }
        const auto& effect =
            std::get<carve_preparation::PreparationSaveRequest>(*request.effect);
        requestRevision = effect.readiness.editRevision();
        requestPin = effect.readiness.pin();
    }

    const auto savedItem = persistOperationOpenItem();
    bool completionAccepted = true;
    if (requestRevision && requestPin) {
        const auto completion = m_preparationFlow.dispatch(
            carve_preparation::CompletePreparationSave{
                *requestPin, *requestRevision, savedItem.has_value()});
        completionAccepted = completion.status ==
            carve_preparation::PrepareCarveTransitionStatus::Applied;
    }
    if (savedItem && completionAccepted) setPreparationDirty(false);
    return completionAccepted ? savedItem : std::nullopt;
}

bool DirectCarvePanel::savePreparation() {
    return syncOperationOpenItem().has_value();
}

} // namespace dw
