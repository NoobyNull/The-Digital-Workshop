#include "prepare_carve_flow.h"

#include <cstddef>
#include <utility>

namespace dw::carve_preparation {
namespace {

constexpr std::array<PreparationStageId, 4> kStageOrder = {
    PreparationStageId::DesignAndSize,
    PreparationStageId::MaterialAndBlank,
    PreparationStageId::ChooseTool,
    PreparationStageId::CarvePreview,
};

std::size_t stageIndex(PreparationStageId stage) noexcept {
    switch (stage) {
    case PreparationStageId::DesignAndSize: return 0;
    case PreparationStageId::MaterialAndBlank: return 1;
    case PreparationStageId::ChooseTool: return 2;
    case PreparationStageId::CarvePreview: return 3;
    }
    return 0;
}

bool validPinShape(const PrepareCarvePin& pin) noexcept {
    return pin.project().valid() && pin.modelItem().valid() && pin.modelSource().valid() &&
           pin.modelSource().kind == workshop::LibraryItemKind::Model &&
           pin.operationItem().valid() && pin.token().valid() &&
           pin.modelItem().project == pin.project() &&
           pin.operationItem().project == pin.project() &&
           pin.modelItem().item != pin.operationItem().item;
}

bool sameReadiness(const PreparationReadinessSnapshot& lhs,
                   const PreparationReadinessSnapshot& rhs) noexcept {
    return lhs.pin() == rhs.pin() && lhs.editRevision() == rhs.editRevision() &&
           lhs.modelLoaded == rhs.modelLoaded &&
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

void addRequirement(PreparationStageProjection& stage,
                    PreparationEvidence evidence,
                    PreparationBlockerCode unknownCode,
                    PreparationBlockerCode unsatisfiedCode) {
    if (evidence == PreparationEvidence::Satisfied)
        return;
    stage.blockers.push_back({stage.id,
                              evidence == PreparationEvidence::Unknown ? unknownCode
                                                                       : unsatisfiedCode,
                              evidence});
}

PreparationStageProjection makeStage(PreparationStageId id,
                                     bool unlocked,
                                     const std::vector<PreparationBlocker>& blockers) {
    PreparationStageProjection stage{id, PreparationStageState::Locked, {}};
    if (!unlocked) {
        stage.blockers.push_back({id,
                                  PreparationBlockerCode::PreviousStageIncomplete,
                                  PreparationEvidence::Unsatisfied});
        return stage;
    }

    stage.blockers = blockers;
    bool hasUnsatisfied = false;
    bool hasUnknown = false;
    for (const auto& blocker : stage.blockers) {
        hasUnsatisfied = hasUnsatisfied ||
                         blocker.evidence == PreparationEvidence::Unsatisfied;
        hasUnknown = hasUnknown || blocker.evidence == PreparationEvidence::Unknown;
    }
    stage.state = hasUnsatisfied ? PreparationStageState::NeedsAttention
                                 : (hasUnknown ? PreparationStageState::Available
                                               : PreparationStageState::Complete);
    return stage;
}

PreparationStageProjection designStage(const PreparationReadinessSnapshot& facts) {
    PreparationStageProjection requirements;
    requirements.id = PreparationStageId::DesignAndSize;
    addRequirement(requirements,
                   facts.modelLoaded,
                   PreparationBlockerCode::DesignLoadUnknown,
                   PreparationBlockerCode::DesignNotLoaded);
    addRequirement(requirements,
                   facts.modelFitsMachine,
                   PreparationBlockerCode::MachineFitUnknown,
                   PreparationBlockerCode::DesignDoesNotFitMachine);
    return makeStage(requirements.id, true, requirements.blockers);
}

PreparationStageProjection materialStage(const PreparationReadinessSnapshot& facts,
                                          bool unlocked) {
    PreparationStageProjection requirements;
    requirements.id = PreparationStageId::MaterialAndBlank;
    addRequirement(requirements,
                   facts.materialSelected,
                   PreparationBlockerCode::MaterialSelectionUnknown,
                   PreparationBlockerCode::MaterialNotSelected);
    addRequirement(requirements,
                   facts.blankSpecified,
                   PreparationBlockerCode::BlankSpecificationUnknown,
                   PreparationBlockerCode::BlankNotSpecified);
    addRequirement(requirements,
                   facts.modelFitsBlank,
                   PreparationBlockerCode::BlankFitUnknown,
                   PreparationBlockerCode::DesignDoesNotFitBlank);
    return makeStage(requirements.id, unlocked, requirements.blockers);
}

PreparationStageProjection toolStage(const PreparationReadinessSnapshot& facts, bool unlocked) {
    PreparationStageProjection requirements;
    requirements.id = PreparationStageId::ChooseTool;
    addRequirement(requirements,
                   facts.finishingToolSelected,
                   PreparationBlockerCode::ToolSelectionUnknown,
                   PreparationBlockerCode::ToolNotSelected);
    addRequirement(requirements,
                   facts.toolSetupConfirmed,
                   PreparationBlockerCode::ToolSetupUnknown,
                   PreparationBlockerCode::ToolSetupNotConfirmed);
    return makeStage(requirements.id, unlocked, requirements.blockers);
}

PreparationStageProjection previewStage(const PreparationReadinessSnapshot& facts,
                                         bool unlocked) {
    PreparationStageProjection requirements;
    requirements.id = PreparationStageId::CarvePreview;
    addRequirement(requirements,
                   facts.toolpathGenerated,
                   PreparationBlockerCode::PreviewGenerationUnknown,
                   PreparationBlockerCode::PreviewNotGenerated);
    addRequirement(requirements,
                   facts.toolpathFresh,
                   PreparationBlockerCode::PreviewFreshnessUnknown,
                   PreparationBlockerCode::PreviewStale);
    return makeStage(requirements.id, unlocked, requirements.blockers);
}

} // namespace

const std::array<PreparationStageId, 4>& preparationStageOrder() noexcept {
    return kStageOrder;
}

PreparationReadinessSnapshot::PreparationReadinessSnapshot(
    PrepareCarvePin pin,
    PreparationEditRevision editRevision) noexcept
    : m_pin(std::move(pin)), m_editRevision(editRevision) {}

const PrepareCarvePin& PreparationReadinessSnapshot::pin() const noexcept {
    return m_pin;
}

PreparationEditRevision PreparationReadinessSnapshot::editRevision() const noexcept {
    return m_editRevision;
}

const PreparationStageProjection&
PrepareCarveFlowSnapshot::stage(PreparationStageId id) const noexcept {
    return stages[stageIndex(id)];
}

const PrepareCarvePin* PrepareCarveFlowSnapshot::pin() const noexcept {
    return readiness ? &readiness->pin() : nullptr;
}

PrepareCarveFlow::PrepareCarveFlow(PrepareCarveFlowOptions options) : m_options(options) {
    resetSnapshot();
}

void PrepareCarveFlow::resetSnapshot() {
    m_snapshot = PrepareCarveFlowSnapshot{};
    for (std::size_t index = 0; index < kStageOrder.size(); ++index)
        m_snapshot.stages[index].id = kStageOrder[index];
}

const PrepareCarveFlowSnapshot& PrepareCarveFlow::snapshot() const noexcept {
    return m_snapshot;
}

PrepareCarveTransition PrepareCarveFlow::dispatch(const PrepareCarveIntent& intent) {
    if (!m_options.enabled) {
        return transition(PrepareCarveTransitionStatus::Disabled,
                          PrepareCarveTransitionReason::ModuleDisabled);
    }
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}

PrepareCarveTransition PrepareCarveFlow::handle(const BeginPreparation& intent) {
    if (m_snapshot.active) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::AlreadyActive);
    }
    if (!validPinShape(intent.readiness.pin())) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::InvalidPin);
    }

    m_snapshot.active = true;
    m_snapshot.activeStage = PreparationStageId::DesignAndSize;
    m_snapshot.readiness = intent.readiness;
    refreshProjection();
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::handle(const RefreshPreparation& intent) {
    if (!m_snapshot.active || !m_snapshot.readiness) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    if (!(m_snapshot.readiness->pin() == intent.readiness.pin())) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::PinMismatch);
    }
    const auto currentRevision = m_snapshot.readiness->editRevision();
    const auto incomingRevision = intent.readiness.editRevision();
    if (incomingRevision < currentRevision) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::StaleRevision);
    }
    if (incomingRevision == currentRevision) {
        return transition(sameReadiness(*m_snapshot.readiness, intent.readiness)
                              ? PrepareCarveTransitionStatus::Unchanged
                              : PrepareCarveTransitionStatus::Rejected,
                          sameReadiness(*m_snapshot.readiness, intent.readiness)
                              ? PrepareCarveTransitionReason::None
                              : PrepareCarveTransitionReason::RevisionConflict);
    }

    m_snapshot.readiness = intent.readiness;
    refreshProjection();
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::handle(const OpenPreparationStage& intent) {
    if (!m_snapshot.active) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    const auto& target = m_snapshot.stage(intent.stage);
    if (target.state == PreparationStageState::Locked) {
        auto result = transition(PrepareCarveTransitionStatus::Rejected,
                                 PrepareCarveTransitionReason::StageLocked);
        if (!target.blockers.empty()) result.blocker = target.blockers.front();
        return result;
    }
    if (m_snapshot.activeStage == intent.stage)
        return transition(PrepareCarveTransitionStatus::Unchanged);

    m_snapshot.activeStage = intent.stage;
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::handle(const ContinuePreparation&) {
    if (!m_snapshot.active) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    const auto& current = m_snapshot.stage(m_snapshot.activeStage);
    if (current.state != PreparationStageState::Complete) {
        auto result = transition(PrepareCarveTransitionStatus::Rejected,
                                 PrepareCarveTransitionReason::StageIncomplete);
        if (!current.blockers.empty()) result.blocker = current.blockers.front();
        return result;
    }

    const auto index = stageIndex(m_snapshot.activeStage);
    if (index + 1 >= kStageOrder.size()) {
        auto result = transition(PrepareCarveTransitionStatus::EffectIssued);
        result.effect = PrepareCarveEffect{PreparationReady{*m_snapshot.readiness}};
        return result;
    }

    m_snapshot.activeStage = kStageOrder[index + 1];
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::handle(const GeneratePreparationPreview&) {
    if (!m_snapshot.active || !m_snapshot.readiness) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    const auto& preview = m_snapshot.stage(PreparationStageId::CarvePreview);
    if (preview.state == PreparationStageState::Locked) {
        auto result = transition(PrepareCarveTransitionStatus::Rejected,
                                 PrepareCarveTransitionReason::StageLocked);
        if (!preview.blockers.empty()) result.blocker = preview.blockers.front();
        return result;
    }

    if (m_snapshot.pendingPreviewRevision == m_snapshot.readiness->editRevision()) {
        return transition(PrepareCarveTransitionStatus::Unchanged,
                          PrepareCarveTransitionReason::RequestPending);
    }

    m_snapshot.activeStage = PreparationStageId::CarvePreview;
    m_snapshot.pendingPreviewRevision = m_snapshot.readiness->editRevision();
    auto result = transition(PrepareCarveTransitionStatus::EffectIssued);
    result.effect = PrepareCarveEffect{PreparationPreviewRequest{*m_snapshot.readiness}};
    result.snapshot = m_snapshot;
    return result;
}

PrepareCarveTransition PrepareCarveFlow::handle(const SavePreparation&) {
    if (!m_snapshot.active || !m_snapshot.readiness) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    if (!m_snapshot.readiness->hasUnsavedChanges) {
        return transition(PrepareCarveTransitionStatus::Unchanged,
                          PrepareCarveTransitionReason::NoUnsavedChanges);
    }

    if (m_snapshot.pendingSaveRevision == m_snapshot.readiness->editRevision()) {
        return transition(PrepareCarveTransitionStatus::Unchanged,
                          PrepareCarveTransitionReason::RequestPending);
    }

    m_snapshot.pendingSaveRevision = m_snapshot.readiness->editRevision();
    auto result = transition(PrepareCarveTransitionStatus::EffectIssued);
    result.effect = PrepareCarveEffect{PreparationSaveRequest{*m_snapshot.readiness}};
    return result;
}

PrepareCarveTransition PrepareCarveFlow::handle(
    const CompletePreparationPreview& intent) {
    if (!m_snapshot.active || !m_snapshot.readiness) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    if (!(m_snapshot.readiness->pin() == intent.pin)) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::PinMismatch);
    }
    if (!m_snapshot.pendingPreviewRevision) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoPendingRequest);
    }
    if (intent.requestRevision != *m_snapshot.pendingPreviewRevision) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::CompletionRevisionMismatch);
    }
    if (intent.requestRevision != m_snapshot.readiness->editRevision()) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::StaleRevision);
    }

    m_snapshot.pendingPreviewRevision.reset();
    m_snapshot.readiness->toolpathGenerated = intent.generated
                                                  ? PreparationEvidence::Satisfied
                                                  : PreparationEvidence::Unsatisfied;
    m_snapshot.readiness->toolpathFresh = intent.generated
                                              ? PreparationEvidence::Satisfied
                                              : PreparationEvidence::Unsatisfied;
    if (intent.generated) m_snapshot.readiness->hasUnsavedChanges = true;
    refreshProjection();
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::handle(const CompletePreparationSave& intent) {
    if (!m_snapshot.active || !m_snapshot.readiness) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    if (!(m_snapshot.readiness->pin() == intent.pin)) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::PinMismatch);
    }
    if (!m_snapshot.pendingSaveRevision) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoPendingRequest);
    }
    if (intent.requestRevision != *m_snapshot.pendingSaveRevision) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::CompletionRevisionMismatch);
    }
    if (intent.requestRevision != m_snapshot.readiness->editRevision()) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::StaleRevision);
    }

    m_snapshot.pendingSaveRevision.reset();
    if (intent.saved) m_snapshot.readiness->hasUnsavedChanges = false;
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::handle(const EndPreparation& intent) {
    if (!m_snapshot.active || !m_snapshot.readiness) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::NoActivePreparation);
    }
    if (!(m_snapshot.readiness->pin() == intent.pin)) {
        return transition(PrepareCarveTransitionStatus::Rejected,
                          PrepareCarveTransitionReason::PinMismatch);
    }

    resetSnapshot();
    return transition(PrepareCarveTransitionStatus::Applied);
}

PrepareCarveTransition PrepareCarveFlow::transition(PrepareCarveTransitionStatus status,
                                                    PrepareCarveTransitionReason reason) const {
    return PrepareCarveTransition{status, reason, m_snapshot, std::nullopt, std::nullopt};
}

void PrepareCarveFlow::refreshProjection() {
    if (!m_snapshot.readiness)
        return;
    const auto& facts = *m_snapshot.readiness;
    m_snapshot.stages[0] = designStage(facts);
    m_snapshot.stages[1] = materialStage(
        facts, m_snapshot.stages[0].state == PreparationStageState::Complete);
    m_snapshot.stages[2] = toolStage(
        facts, m_snapshot.stages[1].state == PreparationStageState::Complete);
    m_snapshot.stages[3] = previewStage(
        facts, m_snapshot.stages[2].state == PreparationStageState::Complete);

    const auto activeIndex = stageIndex(m_snapshot.activeStage);
    for (std::size_t index = 0; index < activeIndex; ++index) {
        if (m_snapshot.stages[index].state != PreparationStageState::Complete) {
            m_snapshot.activeStage = kStageOrder[index];
            break;
        }
    }
}

} // namespace dw::carve_preparation
