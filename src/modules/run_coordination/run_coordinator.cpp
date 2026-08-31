#include "run_coordinator.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace dw::run_coordination {
namespace {

RunTransitionReason reasonForPackageIssue(RunPackageIssue issue) noexcept {
    switch (issue) {
    case RunPackageIssue::None:
        return RunTransitionReason::None;
    case RunPackageIssue::InvalidRunId:
        return RunTransitionReason::InvalidRunId;
    case RunPackageIssue::InvalidPreparationIdentity:
        return RunTransitionReason::InvalidPreparationIdentity;
    case RunPackageIssue::InvalidToolpathIdentity:
        return RunTransitionReason::InvalidToolpathIdentity;
    case RunPackageIssue::SetupIdentityMismatch:
        return RunTransitionReason::SetupIdentityMismatch;
    case RunPackageIssue::InvalidPreflightRevision:
        return RunTransitionReason::InvalidPreflightRevision;
    case RunPackageIssue::PreflightBindingMismatch:
        return RunTransitionReason::PreflightBindingMismatch;
    case RunPackageIssue::PreflightFailed:
        return RunTransitionReason::PreflightFailed;
    }
    return RunTransitionReason::SetupIdentityMismatch;
}

const RunIdentity* commandIdentity(const RunCommand& command) noexcept {
    return std::visit(
        [](const auto& typed) -> const RunIdentity* {
            using Command = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Command, StartRun>)
                return &typed.package.identity();
            else
                return &typed.identity;
        },
        command);
}

} // namespace

bool RunCoordinatorSnapshot::live() const noexcept {
    return state == RunState::Streaming || state == RunState::Paused;
}

const RunIdentity* RunCoordinatorSnapshot::identity() const noexcept {
    return package.has_value() ? &package->identity() : nullptr;
}

RunCommandKind runCommandKind(const RunCommand& command) noexcept {
    return std::visit(
        [](const auto& typed) {
            using Command = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Command, StartRun>)
                return RunCommandKind::Start;
            if constexpr (std::is_same_v<Command, PauseRun>)
                return RunCommandKind::Pause;
            if constexpr (std::is_same_v<Command, ResumeRun>)
                return RunCommandKind::Resume;
            if constexpr (std::is_same_v<Command, AbortRun>)
                return RunCommandKind::Abort;
            if constexpr (std::is_same_v<Command, RunProgressed>)
                return RunCommandKind::Progress;
            if constexpr (std::is_same_v<Command, CompleteRun>)
                return RunCommandKind::Complete;
            return RunCommandKind::Fail;
        },
        command);
}

int runCommandPriority(RunCommandKind kind) noexcept {
    switch (kind) {
    case RunCommandKind::Abort:
        return 0;
    case RunCommandKind::Fail:
        return 1;
    case RunCommandKind::Pause:
        return 2;
    case RunCommandKind::Resume:
        return 3;
    case RunCommandKind::Complete:
        return 4;
    case RunCommandKind::Progress:
        return 5;
    case RunCommandKind::Start:
        return 6;
    }
    return 7;
}

RunCoordinator::RunCoordinator(RunCoordinatorOptions options) : m_options(options) {}

const RunCoordinatorSnapshot& RunCoordinator::snapshot() const noexcept {
    return m_snapshot;
}

RunTransition RunCoordinator::dispatch(const RunCommand& command) {
    if (!m_options.enabled) {
        RunTransition result = transition(RunTransitionStatus::Disabled,
                                          RunTransitionReason::ModuleDisabled);
        result.selectedCommand = runCommandKind(command);
        return result;
    }

    RunTransition result = std::visit(
        [this](const auto& typed) {
            return handle(typed);
        },
        command);
    result.selectedCommand = runCommandKind(command);
    result.snapshot = m_snapshot;
    return result;
}

RunTransition RunCoordinator::dispatchBatch(const std::vector<RunCommand>& commands) {
    if (commands.empty()) {
        return transition(RunTransitionStatus::Rejected,
                          RunTransitionReason::EmptyCommandBatch);
    }
    if (!m_options.enabled) {
        return transition(RunTransitionStatus::Disabled,
                          RunTransitionReason::ModuleDisabled);
    }

    const RunCommand* selected = nullptr;
    int selectedPriority = 8;
    for (const auto& command : commands) {
        if (!eligibleForBatch(command))
            continue;
        const int priority = runCommandPriority(runCommandKind(command));
        if (priority < selectedPriority) {
            selected = &command;
            selectedPriority = priority;
        }
    }

    if (selected == nullptr) {
        selected = &*std::min_element(
            commands.begin(), commands.end(), [](const RunCommand& lhs, const RunCommand& rhs) {
                return runCommandPriority(runCommandKind(lhs)) <
                       runCommandPriority(runCommandKind(rhs));
            });
    }
    return dispatch(*selected);
}

RunTransition RunCoordinator::handle(const StartRun& command) {
    const RunPackageIssue issue = command.package.issue();
    if (issue != RunPackageIssue::None) {
        return transition(RunTransitionStatus::Rejected,
                          reasonForPackageIssue(issue));
    }

    if (m_snapshot.live() || m_snapshot.lockHeld) {
        const RunPackage& active = *m_snapshot.package;
        if (active == command.package) {
            return transition(RunTransitionStatus::Unchanged,
                              RunTransitionReason::RunAlreadyActive);
        }
        if (active.identity().run() == command.package.identity().run()) {
            return transition(RunTransitionStatus::Rejected,
                              RunTransitionReason::PackageIdentityMismatch);
        }
        return transition(RunTransitionStatus::Rejected,
                          RunTransitionReason::RunAlreadyActive);
    }

    if (m_snapshot.package.has_value() &&
        m_snapshot.package->identity().run() == command.package.identity().run()) {
        return transition(RunTransitionStatus::Rejected,
                          RunTransitionReason::RunIdReused);
    }

    m_snapshot.package = command.package;
    m_snapshot.state = RunState::Streaming;
    m_snapshot.lastEventSequence = {};
    m_snapshot.completedFraction = 0.0;
    m_snapshot.lockHeld = true;
    std::vector<RunEffect> effects;
    effects.emplace_back(AcquireRunLock{command.package.identity()});
    effects.emplace_back(StartStream{command.package});
    return transition(RunTransitionStatus::EffectsIssued,
                      RunTransitionReason::None,
                      std::move(effects));
}

RunTransition RunCoordinator::handle(const PauseRun& command) {
    if (const auto invalid = validateActiveIdentity(command.identity))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (m_snapshot.state == RunState::Paused)
        return transition(RunTransitionStatus::Unchanged);
    if (m_snapshot.state != RunState::Streaming)
        return transition(RunTransitionStatus::Rejected, RunTransitionReason::InvalidState);

    m_snapshot.state = RunState::Paused;
    return transition(RunTransitionStatus::EffectsIssued,
                      RunTransitionReason::None,
                      {RunEffect{FeedHold{command.identity}}});
}

RunTransition RunCoordinator::handle(const ResumeRun& command) {
    if (const auto invalid = validateActiveIdentity(command.identity))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (m_snapshot.state == RunState::Streaming)
        return transition(RunTransitionStatus::Unchanged);
    if (m_snapshot.state != RunState::Paused)
        return transition(RunTransitionStatus::Rejected, RunTransitionReason::InvalidState);

    m_snapshot.state = RunState::Streaming;
    return transition(RunTransitionStatus::EffectsIssued,
                      RunTransitionReason::None,
                      {RunEffect{CycleStart{command.identity}}});
}

RunTransition RunCoordinator::handle(const AbortRun& command) {
    if (const auto invalid = validateActiveIdentity(command.identity))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (!m_snapshot.live())
        return transition(RunTransitionStatus::Rejected, RunTransitionReason::InvalidState);

    m_snapshot.state = RunState::Aborted;
    m_snapshot.lockHeld = false;
    return transition(
        RunTransitionStatus::EffectsIssued,
        RunTransitionReason::None,
        {RunEffect{AbortStream{command.identity}},
         RunEffect{ReleaseRunLock{command.identity, RunOutcome::Aborted}}});
}

RunTransition RunCoordinator::handle(const RunProgressed& command) {
    if (const auto invalid = validateActiveIdentity(command.identity))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (m_snapshot.state != RunState::Streaming)
        return transition(RunTransitionStatus::Rejected, RunTransitionReason::InvalidState);
    if (const auto invalid = validateSequence(command.sequence))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (!std::isfinite(command.completedFraction) || command.completedFraction < 0.0 ||
        command.completedFraction > 1.0 ||
        command.completedFraction < m_snapshot.completedFraction) {
        return transition(RunTransitionStatus::Rejected,
                          RunTransitionReason::InvalidProgress);
    }

    m_snapshot.lastEventSequence = command.sequence;
    m_snapshot.completedFraction = command.completedFraction;
    return transition(RunTransitionStatus::Applied);
}

RunTransition RunCoordinator::handle(const CompleteRun& command) {
    if (const auto invalid = validateActiveIdentity(command.identity))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (!m_snapshot.live())
        return transition(RunTransitionStatus::Rejected, RunTransitionReason::InvalidState);
    if (const auto invalid = validateSequence(command.sequence))
        return transition(RunTransitionStatus::Rejected, *invalid);

    m_snapshot.state = RunState::Completed;
    m_snapshot.lastEventSequence = command.sequence;
    m_snapshot.completedFraction = 1.0;
    m_snapshot.lockHeld = false;
    return transition(
        RunTransitionStatus::EffectsIssued,
        RunTransitionReason::None,
        {RunEffect{ReleaseRunLock{command.identity, RunOutcome::Completed}}});
}

RunTransition RunCoordinator::handle(const FailRun& command) {
    if (const auto invalid = validateActiveIdentity(command.identity))
        return transition(RunTransitionStatus::Rejected, *invalid);
    if (!m_snapshot.live())
        return transition(RunTransitionStatus::Rejected, RunTransitionReason::InvalidState);
    if (const auto invalid = validateSequence(command.sequence))
        return transition(RunTransitionStatus::Rejected, *invalid);

    m_snapshot.state = RunState::Failed;
    m_snapshot.lastEventSequence = command.sequence;
    m_snapshot.lockHeld = false;
    return transition(
        RunTransitionStatus::EffectsIssued,
        RunTransitionReason::None,
        {RunEffect{AbortStream{command.identity}},
         RunEffect{ReleaseRunLock{command.identity, RunOutcome::Failed}}});
}

RunTransition RunCoordinator::transition(RunTransitionStatus status,
                                         RunTransitionReason reason,
                                         std::vector<RunEffect> effects) const {
    return {status, reason, m_snapshot, std::move(effects), std::nullopt};
}

std::optional<RunTransitionReason>
RunCoordinator::validateActiveIdentity(const RunIdentity& identity) const noexcept {
    if (!m_snapshot.package.has_value() || !m_snapshot.lockHeld)
        return RunTransitionReason::NoActiveRun;
    const RunIdentity& active = m_snapshot.package->identity();
    if (identity.run() != active.run())
        return RunTransitionReason::RunMismatch;
    if (!(identity == active))
        return RunTransitionReason::PackageIdentityMismatch;
    return std::nullopt;
}

std::optional<RunTransitionReason>
RunCoordinator::validateSequence(RunEventSequence sequence) const noexcept {
    if (!sequence.valid())
        return RunTransitionReason::InvalidEventSequence;
    if (sequence.value <= m_snapshot.lastEventSequence.value)
        return RunTransitionReason::StaleEvent;
    return std::nullopt;
}

bool RunCoordinator::eligibleForBatch(const RunCommand& command) const noexcept {
    const RunIdentity* identity = commandIdentity(command);
    if (m_snapshot.live() && m_snapshot.identity() != nullptr) {
        if (!(*identity == *m_snapshot.identity()))
            return false;
        return std::visit(
            [this](const auto& typed) {
                using Command = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Command, RunProgressed> ||
                              std::is_same_v<Command, CompleteRun> ||
                              std::is_same_v<Command, FailRun>) {
                    return typed.sequence.valid() &&
                           typed.sequence.value > m_snapshot.lastEventSequence.value;
                }
                return true;
            },
            command);
    }
    return std::holds_alternative<StartRun>(command);
}

} // namespace dw::run_coordination
