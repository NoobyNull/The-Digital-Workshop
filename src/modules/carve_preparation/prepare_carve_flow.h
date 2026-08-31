#pragma once

#include <array>
#include <optional>
#include <variant>
#include <vector>

#include "preparation_identity.h"

namespace dw::carve_preparation {

enum class PreparationStageId {
    DesignAndSize,
    MaterialAndBlank,
    ChooseTool,
    CarvePreview,
};

[[nodiscard]] const std::array<PreparationStageId, 4>& preparationStageOrder() noexcept;

enum class PreparationEvidence {
    Unknown,
    Unsatisfied,
    Satisfied,
};

// This is the semantic setup revision (fit, material, tool, or toolpath edits),
// not the project/context generation stored by PrepareCarvePin.
struct PreparationEditRevision {
    std::uint64_t value = 0;

    friend constexpr bool operator==(PreparationEditRevision lhs,
                                     PreparationEditRevision rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(PreparationEditRevision lhs,
                                     PreparationEditRevision rhs) noexcept {
        return !(lhs == rhs);
    }

    friend constexpr bool operator<(PreparationEditRevision lhs,
                                    PreparationEditRevision rhs) noexcept {
        return lhs.value < rhs.value;
    }
};

// The pin cannot be replaced after construction. An adapter may refresh the
// readiness facts, but it must present the same pin to the active flow.
class PreparationReadinessSnapshot final {
  public:
    explicit PreparationReadinessSnapshot(
        PrepareCarvePin pin,
        PreparationEditRevision editRevision = {}) noexcept;

    [[nodiscard]] const PrepareCarvePin& pin() const noexcept;
    [[nodiscard]] PreparationEditRevision editRevision() const noexcept;

    PreparationEvidence modelLoaded = PreparationEvidence::Unknown;
    PreparationEvidence modelFitsMachine = PreparationEvidence::Unknown;
    PreparationEvidence materialSelected = PreparationEvidence::Unknown;
    PreparationEvidence blankSpecified = PreparationEvidence::Unknown;
    PreparationEvidence modelFitsBlank = PreparationEvidence::Unknown;
    PreparationEvidence finishingToolSelected = PreparationEvidence::Unknown;
    PreparationEvidence toolSetupConfirmed = PreparationEvidence::Unknown;
    PreparationEvidence toolpathGenerated = PreparationEvidence::Unknown;
    PreparationEvidence toolpathFresh = PreparationEvidence::Unknown;
    bool hasUnsavedChanges = false;

  private:
    PrepareCarvePin m_pin;
    PreparationEditRevision m_editRevision;
};

enum class PreparationStageState {
    Locked,
    Available,
    NeedsAttention,
    Complete,
};

enum class PreparationBlockerCode {
    PreviousStageIncomplete,
    DesignLoadUnknown,
    DesignNotLoaded,
    MachineFitUnknown,
    DesignDoesNotFitMachine,
    MaterialSelectionUnknown,
    MaterialNotSelected,
    BlankSpecificationUnknown,
    BlankNotSpecified,
    BlankFitUnknown,
    DesignDoesNotFitBlank,
    ToolSelectionUnknown,
    ToolNotSelected,
    ToolSetupUnknown,
    ToolSetupNotConfirmed,
    PreviewGenerationUnknown,
    PreviewNotGenerated,
    PreviewFreshnessUnknown,
    PreviewStale,
};

struct PreparationBlocker {
    PreparationStageId stage = PreparationStageId::DesignAndSize;
    PreparationBlockerCode code = PreparationBlockerCode::PreviousStageIncomplete;
    PreparationEvidence evidence = PreparationEvidence::Unknown;
};

struct PreparationStageProjection {
    PreparationStageId id = PreparationStageId::DesignAndSize;
    PreparationStageState state = PreparationStageState::Locked;
    std::vector<PreparationBlocker> blockers;
};

struct PrepareCarveFlowSnapshot {
    bool active = false;
    PreparationStageId activeStage = PreparationStageId::DesignAndSize;
    std::optional<PreparationReadinessSnapshot> readiness;
    std::array<PreparationStageProjection, 4> stages;
    std::optional<PreparationEditRevision> pendingPreviewRevision;
    std::optional<PreparationEditRevision> pendingSaveRevision;

    [[nodiscard]] const PreparationStageProjection&
    stage(PreparationStageId id) const noexcept;
    [[nodiscard]] const PrepareCarvePin* pin() const noexcept;
};

struct BeginPreparation {
    PreparationReadinessSnapshot readiness;
};

struct RefreshPreparation {
    PreparationReadinessSnapshot readiness;
};

struct OpenPreparationStage {
    PreparationStageId stage = PreparationStageId::DesignAndSize;
};

struct ContinuePreparation {};
struct GeneratePreparationPreview {};
struct SavePreparation {};
struct CompletePreparationPreview {
    PrepareCarvePin pin;
    PreparationEditRevision requestRevision;
    bool generated = false;
};
struct CompletePreparationSave {
    PrepareCarvePin pin;
    PreparationEditRevision requestRevision;
    bool saved = false;
};
struct EndPreparation {
    PrepareCarvePin pin;
};

using PrepareCarveIntent = std::variant<BeginPreparation,
                                        RefreshPreparation,
                                        OpenPreparationStage,
                                        ContinuePreparation,
                                        GeneratePreparationPreview,
                                        SavePreparation,
                                        CompletePreparationPreview,
                                        CompletePreparationSave,
                                        EndPreparation>;

// Effects carry the exact pin and fact snapshot that authorized the request.
// There is deliberately no machine, streaming, or run-control effect here.
struct PreparationPreviewRequest {
    PreparationReadinessSnapshot readiness;
};

struct PreparationSaveRequest {
    PreparationReadinessSnapshot readiness;
};

struct PreparationReady {
    PreparationReadinessSnapshot readiness;
};

using PrepareCarveEffect = std::variant<PreparationPreviewRequest,
                                        PreparationSaveRequest,
                                        PreparationReady>;

enum class PrepareCarveTransitionStatus {
    Applied,
    Unchanged,
    Rejected,
    EffectIssued,
    Disabled,
};

enum class PrepareCarveTransitionReason {
    None,
    ModuleDisabled,
    NoActivePreparation,
    AlreadyActive,
    InvalidPin,
    PinMismatch,
    StageLocked,
    StageIncomplete,
    NoUnsavedChanges,
    RequestPending,
    NoPendingRequest,
    StaleRevision,
    RevisionConflict,
    CompletionRevisionMismatch,
};

struct PrepareCarveTransition {
    PrepareCarveTransitionStatus status = PrepareCarveTransitionStatus::Unchanged;
    PrepareCarveTransitionReason reason = PrepareCarveTransitionReason::None;
    PrepareCarveFlowSnapshot snapshot;
    std::optional<PreparationBlocker> blocker;
    std::optional<PrepareCarveEffect> effect;
};

struct PrepareCarveFlowOptions {
    bool enabled = true;
};

class PrepareCarveFlow final {
  public:
    explicit PrepareCarveFlow(PrepareCarveFlowOptions options = {});

    [[nodiscard]] const PrepareCarveFlowSnapshot& snapshot() const noexcept;
    [[nodiscard]] PrepareCarveTransition dispatch(const PrepareCarveIntent& intent);

  private:
    [[nodiscard]] PrepareCarveTransition handle(const BeginPreparation& intent);
    [[nodiscard]] PrepareCarveTransition handle(const RefreshPreparation& intent);
    [[nodiscard]] PrepareCarveTransition handle(const OpenPreparationStage& intent);
    [[nodiscard]] PrepareCarveTransition handle(const ContinuePreparation& intent);
    [[nodiscard]] PrepareCarveTransition handle(const GeneratePreparationPreview& intent);
    [[nodiscard]] PrepareCarveTransition handle(const SavePreparation& intent);
    [[nodiscard]] PrepareCarveTransition handle(const CompletePreparationPreview& intent);
    [[nodiscard]] PrepareCarveTransition handle(const CompletePreparationSave& intent);
    [[nodiscard]] PrepareCarveTransition handle(const EndPreparation& intent);

    [[nodiscard]] PrepareCarveTransition transition(
        PrepareCarveTransitionStatus status,
        PrepareCarveTransitionReason reason = PrepareCarveTransitionReason::None) const;
    void refreshProjection();
    void resetSnapshot();

    PrepareCarveFlowOptions m_options;
    PrepareCarveFlowSnapshot m_snapshot;
};

} // namespace dw::carve_preparation
