#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "run_package.h"

namespace dw::run_coordination {

struct RunEventSequence {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(RunEventSequence lhs,
                                     RunEventSequence rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

struct StartRun {
    RunPackage package;
};
struct PauseRun {
    RunIdentity identity;
};
struct ResumeRun {
    RunIdentity identity;
};
struct AbortRun {
    RunIdentity identity;
};
struct RunProgressed {
    RunIdentity identity;
    RunEventSequence sequence;
    double completedFraction = 0.0;
};
struct CompleteRun {
    RunIdentity identity;
    RunEventSequence sequence;
};

enum class RunFailure {
    StreamStartFailed,
    ControllerDisconnected,
    ControllerAlarm,
    OperatorEmergencyStop,
    TransportError,
    InvalidMachineResponse,
};

struct FailRun {
    RunIdentity identity;
    RunEventSequence sequence;
    RunFailure failure = RunFailure::TransportError;
};

using RunCommand = std::variant<StartRun,
                                PauseRun,
                                ResumeRun,
                                AbortRun,
                                RunProgressed,
                                CompleteRun,
                                FailRun>;

enum class RunCommandKind {
    Start,
    Pause,
    Resume,
    Abort,
    Progress,
    Complete,
    Fail,
};

struct AcquireRunLock {
    RunIdentity identity;
};
struct StartStream {
    RunPackage package;
};
struct FeedHold {
    RunIdentity identity;
};
struct CycleStart {
    RunIdentity identity;
};
struct AbortStream {
    RunIdentity identity;
};

enum class RunOutcome {
    Completed,
    Aborted,
    Failed,
};

struct ReleaseRunLock {
    RunIdentity identity;
    RunOutcome outcome = RunOutcome::Completed;
};

using RunEffect = std::variant<AcquireRunLock,
                               StartStream,
                               FeedHold,
                               CycleStart,
                               AbortStream,
                               ReleaseRunLock>;

enum class RunState {
    Idle,
    Streaming,
    Paused,
    Completed,
    Aborted,
    Failed,
};

struct RunCoordinatorSnapshot {
    RunState state = RunState::Idle;
    std::optional<RunPackage> package;
    RunEventSequence lastEventSequence;
    double completedFraction = 0.0;
    bool lockHeld = false;

    [[nodiscard]] bool live() const noexcept;
    [[nodiscard]] const RunIdentity* identity() const noexcept;
};

enum class RunTransitionStatus {
    Applied,
    Unchanged,
    EffectsIssued,
    Rejected,
    Disabled,
};

enum class RunTransitionReason {
    None,
    ModuleDisabled,
    EmptyCommandBatch,
    InvalidRunId,
    InvalidPreparationIdentity,
    InvalidToolpathIdentity,
    SetupIdentityMismatch,
    InvalidPreflightRevision,
    PreflightBindingMismatch,
    PreflightFailed,
    NoActiveRun,
    RunAlreadyActive,
    RunIdReused,
    RunMismatch,
    PackageIdentityMismatch,
    InvalidState,
    InvalidEventSequence,
    StaleEvent,
    InvalidProgress,
};

struct RunTransition {
    RunTransitionStatus status = RunTransitionStatus::Unchanged;
    RunTransitionReason reason = RunTransitionReason::None;
    RunCoordinatorSnapshot snapshot;
    std::vector<RunEffect> effects;
    std::optional<RunCommandKind> selectedCommand;
};

struct RunCoordinatorOptions {
    bool enabled = true;
};

class RunCoordinator final {
  public:
    explicit RunCoordinator(RunCoordinatorOptions options = {});

    [[nodiscard]] const RunCoordinatorSnapshot& snapshot() const noexcept;
    [[nodiscard]] RunTransition dispatch(const RunCommand& command);

    // Commands arriving in the same application turn are arbitrated before
    // dispatch. Highest safety priority is Abort, Fail, Pause, Resume,
    // Complete, Progress, then Start. Mismatched stale commands cannot outrank
    // a command for the active immutable identity.
    [[nodiscard]] RunTransition dispatchBatch(const std::vector<RunCommand>& commands);

  private:
    [[nodiscard]] RunTransition handle(const StartRun& command);
    [[nodiscard]] RunTransition handle(const PauseRun& command);
    [[nodiscard]] RunTransition handle(const ResumeRun& command);
    [[nodiscard]] RunTransition handle(const AbortRun& command);
    [[nodiscard]] RunTransition handle(const RunProgressed& command);
    [[nodiscard]] RunTransition handle(const CompleteRun& command);
    [[nodiscard]] RunTransition handle(const FailRun& command);

    [[nodiscard]] RunTransition transition(
        RunTransitionStatus status,
        RunTransitionReason reason = RunTransitionReason::None,
        std::vector<RunEffect> effects = {}) const;
    [[nodiscard]] std::optional<RunTransitionReason>
    validateActiveIdentity(const RunIdentity& identity) const noexcept;
    [[nodiscard]] std::optional<RunTransitionReason>
    validateSequence(RunEventSequence sequence) const noexcept;
    [[nodiscard]] bool eligibleForBatch(const RunCommand& command) const noexcept;

    RunCoordinatorOptions m_options;
    RunCoordinatorSnapshot m_snapshot;
};

[[nodiscard]] RunCommandKind runCommandKind(const RunCommand& command) noexcept;
[[nodiscard]] int runCommandPriority(RunCommandKind kind) noexcept;

} // namespace dw::run_coordination
