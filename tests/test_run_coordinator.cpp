#include <gtest/gtest.h>

#include <array>
#include <variant>
#include <vector>

#include "modules/run_coordination/run_coordinator.h"

namespace {

using namespace dw;
using namespace dw::run_coordination;

constexpr workshop::ProjectId kProject{61};
constexpr workshop::ProjectItemRef kModel{kProject, workshop::ProjectItemId{601}};
constexpr workshop::ProjectItemRef kOperation{kProject, workshop::ProjectItemId{602}};
constexpr workshop::ProjectItemRef kGCode{kProject, workshop::ProjectItemId{603}};
constexpr workshop::LibraryItemRef kSource{workshop::LibraryItemKind::Model,
                                           workshop::LibraryItemId{961}};

carve_preparation::PrepareCarvePin makePin(
    carve_preparation::PreparationToken token = {81}) {
    return {kProject,
            kModel,
            kSource,
            kOperation,
            token,
            carve_preparation::PreparationRevision{14}};
}

RunSetupIdentity makeSetup(
    carve_preparation::PrepareCarvePin pin = makePin(),
    ToolpathRevision toolpathRevision = {17},
    std::string fingerprint = "sha256:ready-gcode") {
    return {std::move(pin),
            ToolpathIdentity(kOperation,
                             kGCode,
                             toolpathRevision,
                             std::move(fingerprint))};
}

RunPackage makePackage(
    workshop::RunId run = workshop::RunId{71},
    RunSetupIdentity setup = makeSetup(),
    RunPreflightFacts facts = RunPreflightFacts::allSatisfied()) {
    RunPreflightSnapshot preflight(setup, PreflightRevision{5}, std::move(facts));
    return {RunIdentity(run, setup), std::move(preflight)};
}

template <typename Effect>
const Effect* effectAt(const RunTransition& transition, std::size_t index) {
    if (index >= transition.effects.size())
        return nullptr;
    const auto& effect = transition.effects.at(index);
    if (!std::holds_alternative<Effect>(effect))
        return nullptr;
    return &std::get<Effect>(effect);
}

} // namespace

TEST(RunCoordinator, StartAcquiresTheExactRunLockBeforeStartingTheExactStream) {
    RunCoordinator coordinator;
    const RunPackage package = makePackage();

    const auto result = coordinator.dispatch(StartRun{package});

    EXPECT_EQ(result.status, RunTransitionStatus::EffectsIssued);
    EXPECT_EQ(result.reason, RunTransitionReason::None);
    ASSERT_EQ(result.effects.size(), 2U);
    const auto* lock = effectAt<AcquireRunLock>(result, 0);
    const auto* stream = effectAt<StartStream>(result, 1);
    ASSERT_NE(lock, nullptr);
    ASSERT_NE(stream, nullptr);
    EXPECT_TRUE(lock->identity == package.identity());
    EXPECT_TRUE(stream->package == package);
    EXPECT_EQ(result.snapshot.state, RunState::Streaming);
    EXPECT_TRUE(result.snapshot.lockHeld);
    ASSERT_NE(result.snapshot.identity(), nullptr);
    EXPECT_TRUE(*result.snapshot.identity() == package.identity());
}

TEST(RunCoordinator, DisabledCoordinatorIsInertAndEmitsNoMachineOrLockEffects) {
    RunCoordinator coordinator(RunCoordinatorOptions{false});

    const auto start = coordinator.dispatch(StartRun{makePackage()});
    EXPECT_EQ(start.status, RunTransitionStatus::Disabled);
    EXPECT_EQ(start.reason, RunTransitionReason::ModuleDisabled);
    EXPECT_TRUE(start.effects.empty());
    EXPECT_EQ(start.snapshot.state, RunState::Idle);
    EXPECT_FALSE(start.snapshot.lockHeld);
    EXPECT_FALSE(start.snapshot.package.has_value());

    const auto batch = coordinator.dispatchBatch(
        {RunCommand{StartRun{makePackage()}},
         RunCommand{AbortRun{makePackage().identity()}}});
    EXPECT_EQ(batch.status, RunTransitionStatus::Disabled);
    EXPECT_TRUE(batch.effects.empty());
    EXPECT_EQ(coordinator.snapshot().state, RunState::Idle);
}

TEST(RunCoordinator, FailedPreflightCannotAcquireALockOrStartStreaming) {
    RunCoordinator coordinator;
    const RunPreflightFacts failed(
        std::array<bool, 7>{true, true, true, false, true, true, true});

    const auto result = coordinator.dispatch(StartRun{makePackage(
        workshop::RunId{71}, makeSetup(), failed)});

    EXPECT_EQ(result.status, RunTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, RunTransitionReason::PreflightFailed);
    EXPECT_TRUE(result.effects.empty());
    EXPECT_EQ(result.snapshot.state, RunState::Idle);
    EXPECT_FALSE(result.snapshot.lockHeld);
}

TEST(RunCoordinator, PauseResumeAndAbortRouteOnlyTheirTypedEffects) {
    RunCoordinator coordinator;
    const RunIdentity identity = makePackage().identity();
    ASSERT_EQ(coordinator.dispatch(StartRun{makePackage()}).status,
              RunTransitionStatus::EffectsIssued);

    const auto paused = coordinator.dispatch(PauseRun{identity});
    EXPECT_EQ(paused.snapshot.state, RunState::Paused);
    ASSERT_EQ(paused.effects.size(), 1U);
    EXPECT_NE(effectAt<FeedHold>(paused, 0), nullptr);

    const auto resumed = coordinator.dispatch(ResumeRun{identity});
    EXPECT_EQ(resumed.snapshot.state, RunState::Streaming);
    ASSERT_EQ(resumed.effects.size(), 1U);
    EXPECT_NE(effectAt<CycleStart>(resumed, 0), nullptr);

    const auto aborted = coordinator.dispatch(AbortRun{identity});
    EXPECT_EQ(aborted.snapshot.state, RunState::Aborted);
    EXPECT_FALSE(aborted.snapshot.lockHeld);
    ASSERT_EQ(aborted.effects.size(), 2U);
    EXPECT_NE(effectAt<AbortStream>(aborted, 0), nullptr);
    const auto* release = effectAt<ReleaseRunLock>(aborted, 1);
    ASSERT_NE(release, nullptr);
    EXPECT_EQ(release->outcome, RunOutcome::Aborted);
}

TEST(RunCoordinator, CompletionReleasesTheLockWithoutIssuingAbort) {
    RunCoordinator coordinator;
    const RunIdentity identity = makePackage().identity();
    ASSERT_EQ(coordinator.dispatch(StartRun{makePackage()}).status,
              RunTransitionStatus::EffectsIssued);

    const auto completed = coordinator.dispatch(
        CompleteRun{identity, RunEventSequence{4}});

    EXPECT_EQ(completed.snapshot.state, RunState::Completed);
    EXPECT_DOUBLE_EQ(completed.snapshot.completedFraction, 1.0);
    EXPECT_FALSE(completed.snapshot.lockHeld);
    ASSERT_EQ(completed.effects.size(), 1U);
    const auto* release = effectAt<ReleaseRunLock>(completed, 0);
    ASSERT_NE(release, nullptr);
    EXPECT_EQ(release->outcome, RunOutcome::Completed);
}

TEST(RunCoordinator, FailureAbortsTheStreamThenReleasesTheLock) {
    RunCoordinator coordinator;
    const RunIdentity identity = makePackage().identity();
    ASSERT_EQ(coordinator.dispatch(StartRun{makePackage()}).status,
              RunTransitionStatus::EffectsIssued);

    const auto failed = coordinator.dispatch(
        FailRun{identity, RunEventSequence{2}, RunFailure::ControllerAlarm});

    EXPECT_EQ(failed.snapshot.state, RunState::Failed);
    EXPECT_FALSE(failed.snapshot.lockHeld);
    ASSERT_EQ(failed.effects.size(), 2U);
    EXPECT_NE(effectAt<AbortStream>(failed, 0), nullptr);
    const auto* release = effectAt<ReleaseRunLock>(failed, 1);
    ASSERT_NE(release, nullptr);
    EXPECT_EQ(release->outcome, RunOutcome::Failed);
}

TEST(RunCoordinator, MismatchedRunAndSetupEventsCannotAffectTheLivePackage) {
    RunCoordinator coordinator;
    const RunPackage package = makePackage();
    ASSERT_EQ(coordinator.dispatch(StartRun{package}).status,
              RunTransitionStatus::EffectsIssued);

    const auto wrongRun = coordinator.dispatch(
        PauseRun{RunIdentity(workshop::RunId{72}, package.identity().setup())});
    EXPECT_EQ(wrongRun.reason, RunTransitionReason::RunMismatch);
    EXPECT_TRUE(wrongRun.effects.empty());

    const RunIdentity alteredSetup(
        package.identity().run(),
        makeSetup(makePin({82}), ToolpathRevision{17}, "sha256:ready-gcode"));
    const auto wrongSetup = coordinator.dispatch(AbortRun{alteredSetup});
    EXPECT_EQ(wrongSetup.reason, RunTransitionReason::PackageIdentityMismatch);
    EXPECT_TRUE(wrongSetup.effects.empty());
    EXPECT_EQ(coordinator.snapshot().state, RunState::Streaming);
    EXPECT_TRUE(*coordinator.snapshot().identity() == package.identity());
}

TEST(RunCoordinator, ASecondStartCannotReplaceTheLiveImmutablePackage) {
    RunCoordinator coordinator;
    const RunPackage original = makePackage();
    ASSERT_EQ(coordinator.dispatch(StartRun{original}).status,
              RunTransitionStatus::EffectsIssued);

    const RunPackage replacement = makePackage(
        workshop::RunId{72},
        makeSetup(makePin({82}), ToolpathRevision{18}, "sha256:replacement"));
    const auto result = coordinator.dispatch(StartRun{replacement});

    EXPECT_EQ(result.status, RunTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, RunTransitionReason::RunAlreadyActive);
    EXPECT_TRUE(result.effects.empty());
    ASSERT_NE(coordinator.snapshot().identity(), nullptr);
    EXPECT_TRUE(*coordinator.snapshot().identity() == original.identity());
}

TEST(RunCoordinator, ProgressRequiresMonotonicSequenceAndFraction) {
    RunCoordinator coordinator;
    const RunIdentity identity = makePackage().identity();
    ASSERT_EQ(coordinator.dispatch(StartRun{makePackage()}).status,
              RunTransitionStatus::EffectsIssued);

    const auto first = coordinator.dispatch(
        RunProgressed{identity, RunEventSequence{3}, 0.4});
    EXPECT_EQ(first.status, RunTransitionStatus::Applied);
    EXPECT_DOUBLE_EQ(first.snapshot.completedFraction, 0.4);

    const auto stale = coordinator.dispatch(
        RunProgressed{identity, RunEventSequence{3}, 0.5});
    EXPECT_EQ(stale.reason, RunTransitionReason::StaleEvent);
    const auto backwards = coordinator.dispatch(
        RunProgressed{identity, RunEventSequence{4}, 0.3});
    EXPECT_EQ(backwards.reason, RunTransitionReason::InvalidProgress);
    EXPECT_DOUBLE_EQ(coordinator.snapshot().completedFraction, 0.4);
}

TEST(RunCoordinator, BatchConflictPriorityIsAbortThenFailPauseResumeCompleteProgressStart) {
    EXPECT_LT(runCommandPriority(RunCommandKind::Abort),
              runCommandPriority(RunCommandKind::Fail));
    EXPECT_LT(runCommandPriority(RunCommandKind::Fail),
              runCommandPriority(RunCommandKind::Pause));
    EXPECT_LT(runCommandPriority(RunCommandKind::Pause),
              runCommandPriority(RunCommandKind::Resume));
    EXPECT_LT(runCommandPriority(RunCommandKind::Resume),
              runCommandPriority(RunCommandKind::Complete));
    EXPECT_LT(runCommandPriority(RunCommandKind::Complete),
              runCommandPriority(RunCommandKind::Progress));
    EXPECT_LT(runCommandPriority(RunCommandKind::Progress),
              runCommandPriority(RunCommandKind::Start));

    RunCoordinator coordinator;
    const RunPackage package = makePackage();
    ASSERT_EQ(coordinator.dispatch(StartRun{package}).status,
              RunTransitionStatus::EffectsIssued);
    const auto result = coordinator.dispatchBatch(
        {RunCommand{RunProgressed{package.identity(), RunEventSequence{1}, 0.1}},
         RunCommand{PauseRun{package.identity()}},
         RunCommand{AbortRun{package.identity()}},
         RunCommand{ResumeRun{package.identity()}}});

    ASSERT_TRUE(result.selectedCommand.has_value());
    EXPECT_EQ(*result.selectedCommand, RunCommandKind::Abort);
    EXPECT_EQ(result.snapshot.state, RunState::Aborted);
    EXPECT_NE(effectAt<AbortStream>(result, 0), nullptr);
}

TEST(RunCoordinator, StaleOrMismatchedSafetyCommandsCannotOutrankMatchingPause) {
    RunCoordinator coordinator;
    const RunPackage package = makePackage();
    ASSERT_EQ(coordinator.dispatch(StartRun{package}).status,
              RunTransitionStatus::EffectsIssued);

    const RunIdentity staleRun(workshop::RunId{99}, package.identity().setup());
    const auto result = coordinator.dispatchBatch(
        {RunCommand{AbortRun{staleRun}},
         RunCommand{PauseRun{package.identity()}}});

    ASSERT_TRUE(result.selectedCommand.has_value());
    EXPECT_EQ(*result.selectedCommand, RunCommandKind::Pause);
    EXPECT_EQ(result.snapshot.state, RunState::Paused);
    EXPECT_NE(effectAt<FeedHold>(result, 0), nullptr);
}

TEST(RunCoordinator, StaleFailureEventCannotOutrankMatchingPause) {
    RunCoordinator coordinator;
    const RunPackage package = makePackage();
    ASSERT_EQ(coordinator.dispatch(StartRun{package}).status,
              RunTransitionStatus::EffectsIssued);
    ASSERT_EQ(coordinator.dispatch(
                  RunProgressed{package.identity(), RunEventSequence{4}, 0.25})
                  .status,
              RunTransitionStatus::Applied);

    const auto result = coordinator.dispatchBatch(
        {RunCommand{FailRun{package.identity(),
                            RunEventSequence{3},
                            RunFailure::TransportError}},
         RunCommand{PauseRun{package.identity()}}});

    ASSERT_TRUE(result.selectedCommand.has_value());
    EXPECT_EQ(*result.selectedCommand, RunCommandKind::Pause);
    EXPECT_EQ(result.snapshot.state, RunState::Paused);
    EXPECT_NE(effectAt<FeedHold>(result, 0), nullptr);
}

TEST(RunCoordinator, ACompletedRunIdCannotBeReusedButANewRunCanStart) {
    RunCoordinator coordinator;
    const RunPackage first = makePackage();
    ASSERT_EQ(coordinator.dispatch(StartRun{first}).status,
              RunTransitionStatus::EffectsIssued);
    ASSERT_EQ(coordinator.dispatch(
                  CompleteRun{first.identity(), RunEventSequence{1}})
                  .status,
              RunTransitionStatus::EffectsIssued);

    const auto reused = coordinator.dispatch(StartRun{first});
    EXPECT_EQ(reused.reason, RunTransitionReason::RunIdReused);
    EXPECT_TRUE(reused.effects.empty());

    const RunPackage next = makePackage(workshop::RunId{72});
    const auto started = coordinator.dispatch(StartRun{next});
    EXPECT_EQ(started.status, RunTransitionStatus::EffectsIssued);
    EXPECT_TRUE(started.snapshot.lockHeld);
    EXPECT_TRUE(*started.snapshot.identity() == next.identity());
}
