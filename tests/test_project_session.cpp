#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include "modules/project_session/project_session.h"

namespace dw::workshop {
namespace {

template <typename Payload>
WorkshopTransition send(ProjectSession& session,
                        Payload payload,
                        std::optional<ContextGeneration> expected = std::nullopt) {
    return session.dispatch(
        WorkshopCommand{WorkshopCommandPayload{std::move(payload)}, expected});
}

ProjectItemRef projectItem(std::int64_t project, std::int64_t item) {
    return {ProjectId(project), ProjectItemId(item)};
}

LibraryItemRef libraryItem(std::int64_t item) {
    return {LibraryItemKind::Model, LibraryItemId(item)};
}

RunLockRef projectRun(std::int64_t run,
                      std::int64_t project,
                      std::int64_t operation) {
    return {RunId(run), projectItem(project, operation)};
}

RunLockRef externalRun(std::int64_t run) {
    return {RunId(run), std::nullopt};
}

void activateAndSelect(ProjectSession& session,
                       std::int64_t project = 1,
                       std::int64_t item = 11) {
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(project)}).accepted());
    ASSERT_TRUE(send(session, SelectProjectItem{projectItem(project, item)}).accepted());
}

} // namespace

TEST(ProjectSession, DefaultsToNeutralHomeContext) {
    const ProjectSession session;
    const auto context = session.snapshot();

    EXPECT_EQ(context.route, WorkshopRoute::Home);
    EXPECT_EQ(context.origin, SelectionOrigin::None);
    EXPECT_FALSE(context.activeProject.has_value());
    EXPECT_FALSE(context.activeProjectItem.has_value());
    EXPECT_FALSE(context.libraryPreview.has_value());
    EXPECT_FALSE(context.activeRun.has_value());
    EXPECT_EQ(context.generation.value, 0U);
}

TEST(ProjectSession, ActivationValidatesBeforeLocksAndIncrementsOnce) {
    ProjectSession session;
    ASSERT_TRUE(send(session, BeginRun{externalRun(1)}).accepted());

    const auto invalid = send(session, ActivateProject{ProjectId()});
    EXPECT_EQ(invalid.status, TransitionStatus::Rejected);
    EXPECT_EQ(invalid.reason, TransitionReason::InvalidReference);
    EXPECT_EQ(invalid.context.generation.value, 1U);

    ASSERT_TRUE(send(session, EndRun{RunId(1)}).accepted());
    const auto activated = send(session, ActivateProject{ProjectId(1)});
    EXPECT_EQ(activated.status, TransitionStatus::Applied);
    EXPECT_EQ(activated.context.activeProject, ProjectId(1));
    EXPECT_EQ(activated.context.generation.value, 3U);

    const auto repeated = send(session, ActivateProject{ProjectId(1)});
    EXPECT_EQ(repeated.status, TransitionStatus::Unchanged);
    EXPECT_EQ(repeated.context.generation.value, 3U);
}

TEST(ProjectSession, StaleGenerationRejectsBeforeDomainEvaluation) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(1)}).accepted());

    const auto stale = send(session, ActivateProject{ProjectId(2)}, ContextGeneration{0});

    EXPECT_EQ(stale.status, TransitionStatus::Rejected);
    EXPECT_EQ(stale.reason, TransitionReason::StaleGeneration);
    EXPECT_EQ(stale.context.activeProject, ProjectId(1));
    EXPECT_EQ(stale.context.generation.value, 1U);
    EXPECT_FALSE(session.hasPendingTransition());
}

TEST(ProjectSession, CloseClearsProjectStateWithoutResettingGeneration) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(send(session, PreviewLibraryItem{libraryItem(101)}).accepted());
    const auto before = session.snapshot().generation.value;

    const auto closed = send(session, CloseProject{});

    EXPECT_EQ(closed.status, TransitionStatus::Applied);
    EXPECT_EQ(closed.context.route, WorkshopRoute::Home);
    EXPECT_FALSE(closed.context.activeProject.has_value());
    EXPECT_FALSE(closed.context.activeProjectItem.has_value());
    EXPECT_FALSE(closed.context.libraryPreview.has_value());
    EXPECT_EQ(closed.context.generation.value, before + 1);

    const auto reopened = send(session, ActivateProject{ProjectId(2)});
    EXPECT_EQ(reopened.context.generation.value, before + 2);
}

TEST(ProjectSession, ProjectItemSelectionEnforcesOwningProject) {
    ProjectSession session;

    const auto noProject = send(session, SelectProjectItem{projectItem(1, 11)});
    EXPECT_EQ(noProject.reason, TransitionReason::NoActiveProject);

    ASSERT_TRUE(send(session, ActivateProject{ProjectId(1)}).accepted());
    const auto invalid = send(session, SelectProjectItem{projectItem(1, 0)});
    EXPECT_EQ(invalid.reason, TransitionReason::InvalidReference);

    const auto mismatch = send(session, SelectProjectItem{projectItem(2, 21)});
    EXPECT_EQ(mismatch.reason, TransitionReason::ProjectMismatch);

    const auto selected = send(session, SelectProjectItem{projectItem(1, 11)});
    EXPECT_EQ(selected.status, TransitionStatus::Applied);
    EXPECT_EQ(selected.context.origin, SelectionOrigin::ProjectItem);
    EXPECT_EQ(selected.context.activeProjectItem->item, ProjectItemId(11));
}

TEST(ProjectSession, SameItemIsIdempotentAndClearRemovesOnlySelection) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());
    const auto generation = session.snapshot().generation;

    const auto same = send(session, SelectProjectItem{projectItem(1, 11)});
    EXPECT_EQ(same.status, TransitionStatus::Unchanged);
    EXPECT_EQ(same.context.generation, generation);

    const auto cleared = send(session, ClearProjectItem{});
    EXPECT_EQ(cleared.status, TransitionStatus::Applied);
    EXPECT_EQ(cleared.context.activeProject, ProjectId(1));
    EXPECT_FALSE(cleared.context.activeProjectItem.has_value());
    EXPECT_TRUE(cleared.context.projectDirty);
    EXPECT_EQ(cleared.context.origin, SelectionOrigin::None);
}

TEST(ProjectSession, LibraryPreviewRequiresExplicitBrowseAndRestoresExactItem) {
    ProjectSession session;
    activateAndSelect(session);

    const auto premature = send(session, PreviewLibraryItem{libraryItem(101)});
    EXPECT_EQ(premature.reason, TransitionReason::InvalidTransition);

    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    const auto preview = send(session, PreviewLibraryItem{libraryItem(101)});
    EXPECT_EQ(preview.status, TransitionStatus::Applied);
    EXPECT_EQ(preview.context.route, WorkshopRoute::DesignLibrary);
    EXPECT_EQ(preview.context.origin, SelectionOrigin::LibraryPreview);
    EXPECT_EQ(preview.context.activeProjectItem->item, ProjectItemId(11));
    EXPECT_EQ(preview.context.libraryReturnRoute, WorkshopRoute::Project);

    const auto returned = send(session, ReturnFromLibrary{});
    EXPECT_EQ(returned.context.route, WorkshopRoute::Project);
    EXPECT_EQ(returned.context.origin, SelectionOrigin::ProjectItem);
    EXPECT_EQ(returned.context.activeProjectItem->item, ProjectItemId(11));
    EXPECT_FALSE(returned.context.libraryPreview.has_value());
}

TEST(ProjectSession, LibraryPreviewReplacementPreservesOriginalReturnRoute) {
    ProjectSession session;
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(send(session, PreviewLibraryItem{libraryItem(101)}).accepted());

    const auto same = send(session, PreviewLibraryItem{libraryItem(101)});
    EXPECT_EQ(same.status, TransitionStatus::Unchanged);

    const auto replacement = send(session, PreviewLibraryItem{libraryItem(102)});
    EXPECT_EQ(replacement.status, TransitionStatus::Applied);
    EXPECT_EQ(replacement.context.libraryReturnRoute, WorkshopRoute::Home);
    EXPECT_EQ(replacement.context.libraryPreview->item, LibraryItemId(102));

    const auto returned = send(session, ReturnFromLibrary{});
    EXPECT_EQ(returned.context.route, WorkshopRoute::Home);
}

TEST(ProjectSession, TemporaryLibraryOverlayPreservesDirtyPreparation) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());
    ASSERT_TRUE(send(session, SetPreparationLock{true}).accepted());

    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(send(session, PreviewLibraryItem{libraryItem(101)}).accepted());
    const auto returned = send(session, ReturnFromLibrary{});

    EXPECT_EQ(returned.status, TransitionStatus::Applied);
    EXPECT_TRUE(returned.context.projectDirty);
    EXPECT_TRUE(returned.context.preparationLocked);
    EXPECT_EQ(returned.context.activeProjectItem->item, ProjectItemId(11));
}

TEST(ProjectSession, ProjectSwitchClearsOldItemPreviewAndGuards) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(send(session, PreviewLibraryItem{libraryItem(101)}).accepted());

    const auto switched = send(session, ActivateProject{ProjectId(2)});

    EXPECT_EQ(switched.status, TransitionStatus::Applied);
    EXPECT_EQ(switched.context.activeProject, ProjectId(2));
    EXPECT_FALSE(switched.context.activeProjectItem.has_value());
    EXPECT_FALSE(switched.context.libraryPreview.has_value());
    EXPECT_FALSE(switched.context.libraryReturnRoute.has_value());
    EXPECT_FALSE(switched.context.projectDirty);
    EXPECT_FALSE(switched.context.preparationLocked);
}

TEST(ProjectSession, HomeRetainsProjectSelectionAndGenericRunNavigationIsRejected) {
    ProjectSession session;
    activateAndSelect(session);

    const auto home = send(session, NavigateTo{WorkshopRoute::Home});
    EXPECT_EQ(home.status, TransitionStatus::Applied);
    EXPECT_EQ(home.context.activeProject, ProjectId(1));
    EXPECT_EQ(home.context.activeProjectItem->item, ProjectItemId(11));

    const auto rawRun = send(session, NavigateTo{WorkshopRoute::RunCnc});
    EXPECT_EQ(rawRun.status, TransitionStatus::Rejected);
    EXPECT_EQ(rawRun.reason, TransitionReason::InvalidTransition);
}

TEST(ProjectSession, ExternalRunHasExplicitIdentityAndReturnsHome) {
    ProjectSession session;

    const auto started = send(session, BeginRun{externalRun(50)});
    EXPECT_EQ(started.status, TransitionStatus::Applied);
    EXPECT_TRUE(started.context.runLocked());
    EXPECT_EQ(started.context.route, WorkshopRoute::RunCnc);
    EXPECT_EQ(started.context.origin, SelectionOrigin::ExternalRun);
    EXPECT_FALSE(started.context.activeProject.has_value());
    EXPECT_EQ(started.context.runReturnRoute, WorkshopRoute::Home);

    const auto wrongFinish = send(session, EndRun{RunId(51)});
    EXPECT_EQ(wrongFinish.reason, TransitionReason::RunMismatch);
    EXPECT_TRUE(wrongFinish.context.runLocked());

    const auto finished = send(session, EndRun{RunId(50)});
    EXPECT_EQ(finished.status, TransitionStatus::Applied);
    EXPECT_FALSE(finished.context.runLocked());
    EXPECT_EQ(finished.context.route, WorkshopRoute::Home);
    EXPECT_EQ(finished.context.origin, SelectionOrigin::None);
}

TEST(ProjectSession, ExternalRunNeverInheritsSelectedProjectAttribution) {
    ProjectSession session;
    activateAndSelect(session);

    const auto started = send(session, BeginRun{externalRun(60)});

    EXPECT_EQ(started.context.origin, SelectionOrigin::ExternalRun);
    ASSERT_TRUE(started.context.activeRun.has_value());
    EXPECT_FALSE(started.context.activeRun->projectPinned());
    EXPECT_EQ(started.context.activeProjectItem->item, ProjectItemId(11));

    const auto finished = send(session, EndRun{RunId(60)});
    EXPECT_EQ(finished.context.route, WorkshopRoute::Project);
    EXPECT_EQ(finished.context.origin, SelectionOrigin::ProjectItem);
}

TEST(ProjectSession, ProjectRunRequiresAndPinsTheSelectedOperation) {
    ProjectSession session;
    activateAndSelect(session);

    const auto mismatch = send(session, BeginRun{projectRun(70, 1, 12)});
    EXPECT_EQ(mismatch.reason, TransitionReason::ProjectMismatch);

    ASSERT_TRUE(send(session, SetPreparationLock{true}).accepted());
    const auto started = send(session, BeginRun{projectRun(70, 1, 11)});
    EXPECT_EQ(started.status, TransitionStatus::Applied);
    EXPECT_TRUE(started.context.activeRun->projectPinned());
    EXPECT_FALSE(started.context.preparationLocked);
    EXPECT_EQ(started.context.origin, SelectionOrigin::ProjectItem);

    const auto finished = send(session, EndRun{RunId(70)});
    EXPECT_EQ(finished.context.route, WorkshopRoute::Project);
    EXPECT_EQ(finished.context.activeProjectItem->item, ProjectItemId(11));
}

TEST(ProjectSession, ActiveRunBlocksContextAndPreparationMutation) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, BeginRun{projectRun(80, 1, 11)}).accepted());

    const std::vector<WorkshopCommand> commands = {
        {ActivateProject{ProjectId(2)}, std::nullopt},
        {CloseProject{}, std::nullopt},
        {SelectProjectItem{projectItem(1, 12)}, std::nullopt},
        {ClearProjectItem{}, std::nullopt},
        {NavigateTo{WorkshopRoute::Home}, std::nullopt},
        {NavigateTo{WorkshopRoute::DesignLibrary}, std::nullopt},
        {PreviewLibraryItem{libraryItem(101)}, std::nullopt},
        {SetPreparationLock{true}, std::nullopt},
    };

    for (const auto& command : commands) {
        const auto blocked = session.dispatch(command);
        EXPECT_EQ(blocked.status, TransitionStatus::Blocked);
        EXPECT_EQ(blocked.reason, TransitionReason::ActiveRun);
        EXPECT_TRUE(blocked.context.runLocked());
    }
}

TEST(ProjectSession, DirtySwitchConfirmationCanCancelWithoutMutation) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());
    const auto generation = session.snapshot().generation;

    const auto confirmation = send(session, ActivateProject{ProjectId(2)});
    ASSERT_EQ(confirmation.status, TransitionStatus::ConfirmationRequired);
    ASSERT_TRUE(confirmation.confirmation.has_value());
    EXPECT_EQ(confirmation.reason, TransitionReason::UnsavedProject);
    EXPECT_TRUE(confirmation.pendingChanges.unsavedProject);
    EXPECT_EQ(confirmation.context.generation, generation);

    const auto cancelled = send(
        session,
        ResolvePendingTransition{*confirmation.confirmation,
                                 PendingTransitionResolution::Cancel});
    EXPECT_EQ(cancelled.status, TransitionStatus::Unchanged);
    EXPECT_EQ(cancelled.context.activeProject, ProjectId(1));
    EXPECT_TRUE(cancelled.context.projectDirty);
    EXPECT_EQ(cancelled.context.generation, generation);
    EXPECT_FALSE(session.hasPendingTransition());
}

TEST(ProjectSession, ConfirmationTokenCannotResolveAnotherRequest) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());

    const auto first = send(session, ActivateProject{ProjectId(2)});
    ASSERT_TRUE(first.confirmation.has_value());

    const auto stale = send(
        session,
        ResolvePendingTransition{ConfirmationToken{999},
                                 PendingTransitionResolution::ChangesResolved});
    EXPECT_EQ(stale.reason, TransitionReason::StaleConfirmation);
    EXPECT_TRUE(session.hasPendingTransition());

    ASSERT_EQ(send(session,
                   ResolvePendingTransition{*first.confirmation,
                                            PendingTransitionResolution::Cancel})
                  .status,
              TransitionStatus::Unchanged);
    const auto second = send(session, ActivateProject{ProjectId(2)});
    ASSERT_TRUE(second.confirmation.has_value());
    EXPECT_NE(*first.confirmation, *second.confirmation);

    const auto replay = send(
        session,
        ResolvePendingTransition{*first.confirmation,
                                 PendingTransitionResolution::ChangesResolved});
    EXPECT_EQ(replay.reason, TransitionReason::StaleConfirmation);
    EXPECT_TRUE(session.hasPendingTransition());
}

TEST(ProjectSession, CombinedDirtyAndPreparationResolutionIsAtomic) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());
    ASSERT_TRUE(send(session, SetPreparationLock{true}).accepted());
    const auto generation = session.snapshot().generation;

    const auto confirmation = send(session, ActivateProject{ProjectId(2)});
    ASSERT_TRUE(confirmation.confirmation.has_value());
    EXPECT_EQ(confirmation.reason, TransitionReason::UnsavedPreparation);
    EXPECT_TRUE(confirmation.pendingChanges.unsavedProject);
    EXPECT_TRUE(confirmation.pendingChanges.unsavedPreparation);
    EXPECT_EQ(confirmation.context.generation, generation);

    const auto resolved = send(
        session,
        ResolvePendingTransition{*confirmation.confirmation,
                                 PendingTransitionResolution::ChangesResolved});
    EXPECT_EQ(resolved.status, TransitionStatus::Applied);
    EXPECT_EQ(resolved.context.activeProject, ProjectId(2));
    EXPECT_FALSE(resolved.context.projectDirty);
    EXPECT_FALSE(resolved.context.preparationLocked);
    EXPECT_EQ(resolved.context.generation.value, generation.value + 1);
}

TEST(ProjectSession, ItemResolutionPreservesProjectDirtyState) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());
    ASSERT_TRUE(send(session, SetPreparationLock{true}).accepted());

    const auto confirmation =
        send(session, SelectProjectItem{projectItem(1, 12)});
    ASSERT_TRUE(confirmation.confirmation.has_value());
    EXPECT_FALSE(confirmation.pendingChanges.unsavedProject);
    EXPECT_TRUE(confirmation.pendingChanges.unsavedPreparation);

    const auto resolved = send(
        session,
        ResolvePendingTransition{*confirmation.confirmation,
                                 PendingTransitionResolution::ChangesResolved});
    EXPECT_EQ(resolved.status, TransitionStatus::Applied);
    EXPECT_EQ(resolved.context.activeProjectItem->item, ProjectItemId(12));
    EXPECT_TRUE(resolved.context.projectDirty);
    EXPECT_FALSE(resolved.context.preparationLocked);
}

TEST(ProjectSession, PendingTransitionBlocksReplacementButValidRunPreempts) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetProjectDirty{true}).accepted());
    ASSERT_EQ(send(session, ActivateProject{ProjectId(2)}).status,
              TransitionStatus::ConfirmationRequired);

    const auto blocked = send(session, NavigateTo{WorkshopRoute::Home});
    EXPECT_EQ(blocked.status, TransitionStatus::Blocked);
    EXPECT_EQ(blocked.reason, TransitionReason::PendingConfirmation);

    const auto invalidRun = send(session, BeginRun{externalRun(0)});
    EXPECT_EQ(invalidRun.reason, TransitionReason::InvalidReference);
    EXPECT_TRUE(session.hasPendingTransition());

    const auto started = send(session, BeginRun{projectRun(90, 1, 11)});
    EXPECT_EQ(started.status, TransitionStatus::Applied);
    EXPECT_TRUE(started.context.runLocked());
    EXPECT_FALSE(session.hasPendingTransition());
}

TEST(ProjectSession, ExternalRunCannotDiscardLockedProjectPreparation) {
    ProjectSession session;
    activateAndSelect(session);
    ASSERT_TRUE(send(session, SetPreparationLock{true}).accepted());

    const auto blocked = send(session, BeginRun{externalRun(100)});

    EXPECT_EQ(blocked.status, TransitionStatus::Blocked);
    EXPECT_EQ(blocked.reason, TransitionReason::UnsavedPreparation);
    EXPECT_TRUE(blocked.context.preparationLocked);
    EXPECT_FALSE(blocked.context.runLocked());
}

TEST(ProjectSession, DirtyAndPreparationStateRequireValidProjectContext) {
    ProjectSession session;

    EXPECT_EQ(send(session, SetProjectDirty{true}).reason,
              TransitionReason::NoActiveProject);
    EXPECT_EQ(send(session, SetPreparationLock{true}).reason,
              TransitionReason::InvalidTransition);
    EXPECT_EQ(send(session, ReturnFromLibrary{}).status,
              TransitionStatus::Unchanged);

    ASSERT_TRUE(send(session, ActivateProject{ProjectId(1)}).accepted());
    EXPECT_EQ(send(session, SetPreparationLock{true}).reason,
              TransitionReason::InvalidTransition);
}

} // namespace dw::workshop
