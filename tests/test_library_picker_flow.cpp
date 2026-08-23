#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "modules/design_library/library_picker_flow.h"

namespace dw::design_library {
namespace {

workshop::LibraryItemRef model(std::int64_t id) {
    return {workshop::LibraryItemKind::Model, workshop::LibraryItemId(id)};
}

workshop::LibraryItemRef gcode(std::int64_t id) {
    return {workshop::LibraryItemKind::GCode, workshop::LibraryItemId(id)};
}

bool sameItem(workshop::LibraryItemRef lhs, workshop::LibraryItemRef rhs) {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

workshop::WorkshopContextSnapshot homeSession() {
    workshop::WorkshopContextSnapshot session;
    session.route = workshop::WorkshopRoute::Home;
    return session;
}

workshop::WorkshopContextSnapshot projectSession(
    std::int64_t projectId, std::optional<std::int64_t> itemId = std::nullopt) {
    workshop::WorkshopContextSnapshot session;
    session.activeProject = workshop::ProjectId(projectId);
    session.route = workshop::WorkshopRoute::Project;
    session.origin = workshop::SelectionOrigin::None;
    if (itemId.has_value()) {
        session.activeProjectItem = workshop::ProjectItemRef{*session.activeProject,
                                                             workshop::ProjectItemId(*itemId)};
        session.origin = workshop::SelectionOrigin::ProjectItem;
    }
    return session;
}

void showLibrary(workshop::WorkshopContextSnapshot& session) {
    session.libraryReturnRoute = session.route;
    session.route = workshop::WorkshopRoute::DesignLibrary;
}

void applyRestore(workshop::WorkshopContextSnapshot& session,
                  const RestoreLibraryContextRequest& restore) {
    session.route = restore.route;
    session.activeProject = restore.project;
    session.activeProjectItem = restore.projectItem;
    session.libraryPreview.reset();
    session.libraryReturnRoute.reset();
    ++session.generation.value;
}

LibraryPickerTransition begin(LibraryPickerFlow& flow,
                              LibraryPickerPurpose purpose,
                              const workshop::WorkshopContextSnapshot& session,
                              std::string projectName = {},
                              std::vector<workshop::LibraryItemRef> membership = {}) {
    return flow.dispatch(BeginLibraryPicker{purpose, std::move(projectName), std::move(membership)},
                         session);
}

TEST(LibraryPickerFlow, ManagePurposeHasNoImplicitMembershipAction) {
    LibraryPickerFlow flow;
    auto session = homeSession();

    const auto opened = begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    ASSERT_EQ(opened.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_TRUE(opened.snapshot.active);
    EXPECT_TRUE(opened.snapshot.primaryActionLabel().empty());

    showLibrary(session);
    const auto selected = flow.dispatch(ReplaceLibrarySelection{{model(7)}}, session);
    EXPECT_EQ(selected.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_FALSE(selected.request.has_value());
    EXPECT_FALSE(selected.snapshot.previewItem.has_value());
    EXPECT_FALSE(selected.snapshot.pendingPreviewToken.has_value());

    const auto unselectedPreview = flow.dispatch(RequestLibraryPreview{model(8)}, session);
    EXPECT_EQ(unselectedPreview.reason, LibraryPickerTransitionReason::ItemNotSelected);
    EXPECT_FALSE(unselectedPreview.request.has_value());

    const auto confirmed = flow.dispatch(ConfirmLibrarySelection{}, session);
    EXPECT_EQ(confirmed.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(confirmed.reason, LibraryPickerTransitionReason::NoPrimaryAction);
    EXPECT_FALSE(confirmed.request.has_value());
}

TEST(LibraryPickerFlow, AddPurposeRequiresAndPinsProjectIdentity) {
    LibraryPickerFlow flow;
    auto session = homeSession();

    const auto withoutProject =
        begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    EXPECT_EQ(withoutProject.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(withoutProject.reason, LibraryPickerTransitionReason::NoActiveProject);

    session = projectSession(41, 91);
    const auto unnamed = begin(flow, LibraryPickerPurpose::AddToProject, session, "  ");
    EXPECT_EQ(unnamed.status, LibraryPickerTransitionStatus::Rejected);

    const auto opened =
        begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign", {model(3)});
    ASSERT_EQ(opened.status, LibraryPickerTransitionStatus::Applied);
    ASSERT_TRUE(opened.snapshot.activeProject.has_value());
    EXPECT_EQ(*opened.snapshot.activeProject, workshop::ProjectId(41));
    EXPECT_EQ(opened.snapshot.activeProjectName, "River Sign");
    EXPECT_EQ(opened.snapshot.primaryActionLabel(), "Choose this model");
}

TEST(LibraryPickerFlow, ProjectChooserRejectsMultipleModelsAndGCode) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    ASSERT_EQ(
        begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign", {model(3)}).status,
        LibraryPickerTransitionStatus::Applied);
    showLibrary(session);

    const auto mixed =
        flow.dispatch(ReplaceLibrarySelection{{model(8), model(8), gcode(12), model(8)}}, session);

    EXPECT_EQ(mixed.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(mixed.reason, LibraryPickerTransitionReason::SingleModelRequired);
    EXPECT_TRUE(mixed.snapshot.selectedItems.empty());

    const auto gcodeOnly =
        flow.dispatch(ReplaceLibrarySelection{{gcode(12)}}, session);
    EXPECT_EQ(gcodeOnly.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(gcodeOnly.reason, LibraryPickerTransitionReason::SingleModelRequired);

    const auto selected = flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
    ASSERT_EQ(selected.status, LibraryPickerTransitionStatus::Applied);
    ASSERT_EQ(selected.snapshot.selectedItems.size(), 1u);
    EXPECT_TRUE(sameItem(selected.snapshot.selectedItems.front(), model(8)));
    EXPECT_FALSE(selected.request.has_value());
}

TEST(LibraryPickerFlow, PreviewAndProjectMembershipRemainIndependent) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign", {model(3)});
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(22)}}, session);

    const auto requested = flow.dispatch(RequestLibraryPreview{model(22)}, session);

    ASSERT_EQ(requested.status, LibraryPickerTransitionStatus::RequestIssued);
    const auto* preview = std::get_if<PreviewLibraryItemRequest>(&*requested.request);
    ASSERT_NE(preview, nullptr);
    EXPECT_TRUE(preview->token.valid());
    EXPECT_TRUE(sameItem(preview->item, model(22)));
    ASSERT_EQ(requested.snapshot.selectedItems.size(), 1u);
    EXPECT_TRUE(sameItem(requested.snapshot.selectedItems.front(), model(22)));
    EXPECT_TRUE(requested.snapshot.isProjectMember(model(3)));
    EXPECT_FALSE(requested.snapshot.isProjectMember(model(22)));
    EXPECT_FALSE(requested.snapshot.previewItem.has_value());

    const auto completed = flow.dispatch(CompleteLibraryPreview{preview->token, true}, session);
    ASSERT_EQ(completed.status, LibraryPickerTransitionStatus::Applied);
    ASSERT_TRUE(completed.snapshot.previewItem.has_value());
    EXPECT_TRUE(sameItem(*completed.snapshot.previewItem, model(22)));
    EXPECT_TRUE(sameItem(completed.snapshot.selectedItems.front(), model(22)));
    EXPECT_FALSE(completed.snapshot.isProjectMember(model(22)));
}

TEST(LibraryPickerFlow, StalePreviewCompletionCannotReplaceNewerPreview) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(1), model(2)}}, session);

    const auto first = flow.dispatch(RequestLibraryPreview{model(1)}, session);
    const auto second = flow.dispatch(RequestLibraryPreview{model(2)}, session);
    const auto* firstRequest = std::get_if<PreviewLibraryItemRequest>(&*first.request);
    const auto* secondRequest = std::get_if<PreviewLibraryItemRequest>(&*second.request);
    ASSERT_NE(firstRequest, nullptr);
    ASSERT_NE(secondRequest, nullptr);
    EXPECT_NE(firstRequest->token, secondRequest->token);

    const auto stale = flow.dispatch(CompleteLibraryPreview{firstRequest->token, true}, session);
    EXPECT_EQ(stale.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(stale.reason, LibraryPickerTransitionReason::StaleCompletion);
    ASSERT_TRUE(stale.snapshot.pendingPreviewItem.has_value());
    EXPECT_TRUE(sameItem(*stale.snapshot.pendingPreviewItem, model(2)));
    EXPECT_FALSE(stale.snapshot.previewItem.has_value());

    const auto current = flow.dispatch(CompleteLibraryPreview{secondRequest->token, true}, session);
    ASSERT_TRUE(current.snapshot.previewItem.has_value());
    EXPECT_TRUE(sameItem(*current.snapshot.previewItem, model(2)));
}

TEST(LibraryPickerFlow, SemanticSelectionReplayKeepsPendingPreviewGeneration) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(1), model(2)}}, session);
    const auto requested = flow.dispatch(RequestLibraryPreview{model(1)}, session);
    const auto* preview = std::get_if<PreviewLibraryItemRequest>(&*requested.request);
    ASSERT_NE(preview, nullptr);

    const auto replayed = flow.dispatch(ReplaceLibrarySelection{{model(2), model(1), model(2)}},
                                        session);

    EXPECT_EQ(replayed.status, LibraryPickerTransitionStatus::Unchanged);
    EXPECT_EQ(replayed.snapshot.pendingPreviewToken, preview->token);
    ASSERT_TRUE(replayed.snapshot.pendingPreviewItem.has_value());
    EXPECT_TRUE(sameItem(*replayed.snapshot.pendingPreviewItem, model(1)));
    const auto completed = flow.dispatch(CompleteLibraryPreview{preview->token, true}, session);
    ASSERT_TRUE(completed.snapshot.previewItem.has_value());
    EXPECT_TRUE(sameItem(*completed.snapshot.previewItem, model(1)));
}

TEST(LibraryPickerFlow, SelectionReplacementInvalidatesPendingPreviewAndKeepsLastSuccess) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(1)}}, session);
    const auto first = flow.dispatch(RequestLibraryPreview{model(1)}, session);
    const auto* firstRequest = std::get_if<PreviewLibraryItemRequest>(&*first.request);
    ASSERT_NE(firstRequest, nullptr);
    flow.dispatch(CompleteLibraryPreview{firstRequest->token, true}, session);

    flow.dispatch(ReplaceLibrarySelection{{model(2)}}, session);
    const auto replacement = flow.dispatch(RequestLibraryPreview{model(2)}, session);
    const auto* replacementRequest = std::get_if<PreviewLibraryItemRequest>(&*replacement.request);
    ASSERT_NE(replacementRequest, nullptr);
    const auto failed = flow.dispatch(CompleteLibraryPreview{replacementRequest->token, false},
                                      session);
    ASSERT_TRUE(failed.snapshot.previewItem.has_value());
    EXPECT_TRUE(sameItem(*failed.snapshot.previewItem, model(1)));

    const auto pending = flow.dispatch(RequestLibraryPreview{model(2)}, session);
    const auto* pendingRequest = std::get_if<PreviewLibraryItemRequest>(&*pending.request);
    ASSERT_NE(pendingRequest, nullptr);
    const auto replaced = flow.dispatch(ReplaceLibrarySelection{{model(3)}}, session);
    EXPECT_FALSE(replaced.snapshot.pendingPreviewItem.has_value());
    EXPECT_FALSE(replaced.snapshot.pendingPreviewToken.has_value());
    ASSERT_TRUE(replaced.snapshot.previewItem.has_value());
    EXPECT_TRUE(sameItem(*replaced.snapshot.previewItem, model(1)));
    EXPECT_EQ(flow.dispatch(CompleteLibraryPreview{pendingRequest->token, true}, session).reason,
              LibraryPickerTransitionReason::StaleCompletion);
}

TEST(LibraryPickerFlow, ImportedItemsRespectSingleModelChoice) {
    LibraryPickerFlow flow;
    auto session = projectSession(41);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign", {model(3)});
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);

    const auto rejected = flow.dispatch(
        OfferImportedLibraryItems{{model(8), model(10), model(10), gcode(12)}}, session);

    ASSERT_EQ(rejected.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(rejected.reason, LibraryPickerTransitionReason::SingleModelRequired);
    ASSERT_EQ(rejected.snapshot.selectedItems.size(), 1u);
    EXPECT_TRUE(sameItem(rejected.snapshot.selectedItems.front(), model(8)));

    const auto offered =
        flow.dispatch(OfferImportedLibraryItems{{model(10)}}, session);
    ASSERT_EQ(offered.status, LibraryPickerTransitionStatus::Applied);
    ASSERT_EQ(offered.snapshot.selectedItems.size(), 1u);
    EXPECT_TRUE(sameItem(offered.snapshot.selectedItems.front(), model(10)));
    EXPECT_FALSE(offered.snapshot.previewItem.has_value());
    EXPECT_FALSE(offered.snapshot.pendingPreviewToken.has_value());
    EXPECT_TRUE(offered.snapshot.isProjectMember(model(3)));
    EXPECT_FALSE(offered.snapshot.isProjectMember(model(10)));
    EXPECT_FALSE(offered.snapshot.pendingActionToken.has_value());
    EXPECT_FALSE(offered.request.has_value());
}

TEST(LibraryPickerFlow, ChoiceIssuesOneModelRequestAndSuppressesDoubleSubmit) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);

    const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);

    ASSERT_EQ(requested.status, LibraryPickerTransitionStatus::RequestIssued);
    const auto* add = std::get_if<EnsureLibraryItemsInProjectRequest>(&*requested.request);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->project, workshop::ProjectId(41));
    ASSERT_EQ(add->items.size(), 1u);
    EXPECT_TRUE(sameItem(add->items[0], model(8)));
    EXPECT_FALSE(requested.snapshot.isProjectMember(model(8)));
    EXPECT_TRUE(requested.snapshot.isAddPending(model(8)));

    const auto repeated = flow.dispatch(ConfirmLibrarySelection{}, session);
    EXPECT_EQ(repeated.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(repeated.reason, LibraryPickerTransitionReason::RequestPending);
    EXPECT_FALSE(repeated.request.has_value());
}

TEST(LibraryPickerFlow, ConfirmedMembershipMakesRepeatedAddIdempotent) {
    LibraryPickerFlow flow;
    auto session = projectSession(41);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
    const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
    const auto* add = std::get_if<EnsureLibraryItemsInProjectRequest>(&*requested.request);
    ASSERT_NE(add, nullptr);

    const auto completed =
        flow.dispatch(CompleteLibraryAdd{add->token, workshop::ProjectId(41), {model(8)}}, session);
    ASSERT_EQ(completed.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_TRUE(completed.snapshot.isProjectMember(model(8)));
    EXPECT_FALSE(completed.snapshot.isAddPending(model(8)));

    const auto repeated = flow.dispatch(ConfirmLibrarySelection{}, session);
    EXPECT_EQ(repeated.status, LibraryPickerTransitionStatus::Unchanged);
    EXPECT_EQ(repeated.reason, LibraryPickerTransitionReason::AlreadyMember);
    EXPECT_FALSE(repeated.request.has_value());
}

TEST(LibraryPickerFlow, ExistingProjectModelBlocksASecondChoice) {
    LibraryPickerFlow flow;
    auto session = projectSession(41);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign", {model(8)});
    showLibrary(session);
    ASSERT_EQ(flow.dispatch(ReplaceLibrarySelection{{model(9)}}, session).status,
              LibraryPickerTransitionStatus::Applied);

    const auto blocked = flow.dispatch(ConfirmLibrarySelection{}, session);

    EXPECT_EQ(blocked.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(blocked.reason,
              LibraryPickerTransitionReason::ProjectAlreadyHasDesign);
    EXPECT_FALSE(blocked.request.has_value());
    EXPECT_FALSE(blocked.snapshot.pendingActionToken.has_value());
}

TEST(LibraryPickerFlow, ProjectChangeRejectsAddWithoutEmittingARequest) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);

    session.activeProject = workshop::ProjectId(42);
    session.activeProjectItem.reset();
    const auto changed = flow.dispatch(ConfirmLibrarySelection{}, session);

    EXPECT_EQ(changed.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(changed.reason, LibraryPickerTransitionReason::ContextChanged);
    EXPECT_FALSE(changed.request.has_value());
    ASSERT_TRUE(flow.snapshot().activeProject.has_value());
    EXPECT_EQ(*flow.snapshot().activeProject, workshop::ProjectId(41));
}

TEST(LibraryPickerFlow, AsyncCompletionsCannotCommitAfterLiveContextChanges) {
    {
        LibraryPickerFlow flow;
        auto session = homeSession();
        begin(flow, LibraryPickerPurpose::ManageLibrary, session);
        showLibrary(session);
        flow.dispatch(ReplaceLibrarySelection{{model(1)}}, session);
        const auto requested = flow.dispatch(RequestLibraryPreview{model(1)}, session);
        const auto* preview = std::get_if<PreviewLibraryItemRequest>(&*requested.request);
        ASSERT_NE(preview, nullptr);
        session.route = workshop::WorkshopRoute::Home;
        const auto completed = flow.dispatch(CompleteLibraryPreview{preview->token, true}, session);
        EXPECT_EQ(completed.reason, LibraryPickerTransitionReason::PickerNotVisible);
        EXPECT_FALSE(completed.snapshot.previewItem.has_value());
    }
    {
        LibraryPickerFlow flow;
        auto session = projectSession(41);
        begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
        showLibrary(session);
        flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
        const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
        const auto* add = std::get_if<EnsureLibraryItemsInProjectRequest>(&*requested.request);
        ASSERT_NE(add, nullptr);
        session.activeProject = workshop::ProjectId(42);
        const auto completed = flow.dispatch(
            CompleteLibraryAdd{add->token, workshop::ProjectId(41), {model(8)}}, session);
        EXPECT_EQ(completed.reason, LibraryPickerTransitionReason::ContextChanged);
        EXPECT_FALSE(completed.snapshot.isProjectMember(model(8)));
    }
    {
        LibraryPickerFlow flow;
        auto session = homeSession();
        begin(flow, LibraryPickerPurpose::StartProject, session);
        showLibrary(session);
        flow.dispatch(ReplaceLibrarySelection{{model(2)}}, session);
        const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
        const auto* start = std::get_if<StartProjectWithLibraryItemRequest>(&*requested.request);
        ASSERT_NE(start, nullptr);
        session.route = workshop::WorkshopRoute::Home;
        const auto completed =
            flow.dispatch(CompleteStartProject{start->token, workshop::ProjectId(42)}, session);
        EXPECT_EQ(completed.reason, LibraryPickerTransitionReason::ContextChanged);
        EXPECT_TRUE(completed.snapshot.active);
    }
}

TEST(LibraryPickerFlow, ActionCompletionCannotCrossPickerPurposes) {
    {
        LibraryPickerFlow flow;
        auto session = homeSession();
        session.activeProject = workshop::ProjectId(41);
        begin(flow, LibraryPickerPurpose::StartProject, session);
        showLibrary(session);
        flow.dispatch(ReplaceLibrarySelection{{model(2)}}, session);
        const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
        const auto* start = std::get_if<StartProjectWithLibraryItemRequest>(&*requested.request);
        ASSERT_NE(start, nullptr);

        const auto wrong =
            flow.dispatch(CompleteLibraryAdd{start->token, workshop::ProjectId(41), {}}, session);
        EXPECT_EQ(wrong.reason, LibraryPickerTransitionReason::WrongPurpose);
        EXPECT_TRUE(wrong.snapshot.startRequestPending);
        EXPECT_EQ(wrong.snapshot.pendingActionToken, start->token);
        EXPECT_EQ(flow.dispatch(CompleteStartProject{start->token, std::nullopt}, session).status,
                  LibraryPickerTransitionStatus::Applied);
    }
    {
        LibraryPickerFlow flow;
        auto session = projectSession(41);
        begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
        showLibrary(session);
        flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
        const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
        const auto* add = std::get_if<EnsureLibraryItemsInProjectRequest>(&*requested.request);
        ASSERT_NE(add, nullptr);

        const auto wrong = flow.dispatch(CompleteStartProject{add->token, std::nullopt}, session);
        EXPECT_EQ(wrong.reason, LibraryPickerTransitionReason::WrongPurpose);
        EXPECT_TRUE(wrong.snapshot.isAddPending(model(8)));
        EXPECT_EQ(wrong.snapshot.pendingActionToken, add->token);
        EXPECT_EQ(flow.dispatch(CompleteLibraryAdd{add->token, workshop::ProjectId(41), {model(8)}},
                                session)
                      .status,
                  LibraryPickerTransitionStatus::Applied);
    }
}

TEST(LibraryPickerFlow, StartProjectRequiresExactlyOneModelAndCanRetryFailure) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    const auto opened = begin(flow, LibraryPickerPurpose::StartProject, session);
    EXPECT_EQ(opened.snapshot.primaryActionLabel(), "Start project with this model");
    showLibrary(session);

    const auto multiple =
        flow.dispatch(ReplaceLibrarySelection{{model(1), model(2)}}, session);
    EXPECT_EQ(multiple.reason, LibraryPickerTransitionReason::SingleModelRequired);

    const auto wrongKind =
        flow.dispatch(ReplaceLibrarySelection{{gcode(9)}}, session);
    EXPECT_EQ(wrongKind.reason, LibraryPickerTransitionReason::SingleModelRequired);

    flow.dispatch(ReplaceLibrarySelection{{model(2)}}, session);
    const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
    const auto* start = std::get_if<StartProjectWithLibraryItemRequest>(&*requested.request);
    ASSERT_NE(start, nullptr);
    EXPECT_TRUE(sameItem(start->item, model(2)));
    EXPECT_FALSE(requested.snapshot.canConfirm());

    const auto failed = flow.dispatch(CompleteStartProject{start->token, std::nullopt}, session);
    EXPECT_TRUE(failed.snapshot.active);
    EXPECT_TRUE(failed.snapshot.canConfirm());
    const auto retry = flow.dispatch(ConfirmLibrarySelection{}, session);
    const auto* retriedStart = std::get_if<StartProjectWithLibraryItemRequest>(&*retry.request);
    ASSERT_NE(retriedStart, nullptr);
    EXPECT_NE(retriedStart->token, start->token);
}

TEST(LibraryPickerFlow, StartSuccessRequiresAndAcceptsTheNewlyActivatedProject) {
    for (const auto oldProject : {std::optional<workshop::ProjectId>{},
                                  std::optional<workshop::ProjectId>{workshop::ProjectId(41)}}) {
        LibraryPickerFlow flow;
        auto session = homeSession();
        session.activeProject = oldProject;
        begin(flow, LibraryPickerPurpose::StartProject, session);
        showLibrary(session);
        flow.dispatch(ReplaceLibrarySelection{{model(2)}}, session);
        const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
        const auto* start = std::get_if<StartProjectWithLibraryItemRequest>(&*requested.request);
        ASSERT_NE(start, nullptr);

        session = projectSession(42);
        const auto completed =
            flow.dispatch(CompleteStartProject{start->token, workshop::ProjectId(42)}, session);
        EXPECT_EQ(completed.status, LibraryPickerTransitionStatus::Applied);
        EXPECT_FALSE(completed.snapshot.active);
    }
}

TEST(LibraryPickerFlow, CancelRestoresExactContextAndInvalidatesPendingPreview) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(22)}}, session);
    const auto previewed = flow.dispatch(RequestLibraryPreview{model(22)}, session);
    const auto* preview = std::get_if<PreviewLibraryItemRequest>(&*previewed.request);
    ASSERT_NE(preview, nullptr);

    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);

    ASSERT_EQ(cancelled.status, LibraryPickerTransitionStatus::RequestIssued);
    const auto* restore = std::get_if<RestoreLibraryContextRequest>(&*cancelled.request);
    ASSERT_NE(restore, nullptr);
    EXPECT_EQ(restore->route, workshop::WorkshopRoute::Project);
    ASSERT_TRUE(restore->project.has_value());
    EXPECT_EQ(*restore->project, workshop::ProjectId(41));
    ASSERT_TRUE(restore->projectItem.has_value());
    EXPECT_EQ(*restore->projectItem,
              (workshop::ProjectItemRef{workshop::ProjectId(41), workshop::ProjectItemId(91)}));
    EXPECT_TRUE(restore->token.valid());
    EXPECT_EQ(restore->expectedGeneration, session.generation);
    EXPECT_TRUE(cancelled.snapshot.active);
    EXPECT_TRUE(cancelled.snapshot.returnPending);
    EXPECT_EQ(cancelled.snapshot.pendingRestoreToken, restore->token);
    EXPECT_FALSE(cancelled.snapshot.pendingPreviewToken.has_value());
    EXPECT_FALSE(cancelled.snapshot.pendingActionToken.has_value());

    applyRestore(session, *restore);
    const auto completed = flow.dispatch(CompleteLibraryRestore{restore->token, true}, session);
    EXPECT_EQ(completed.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_FALSE(completed.snapshot.active);

    const auto latePreview = flow.dispatch(CompleteLibraryPreview{preview->token, true}, session);
    EXPECT_EQ(latePreview.reason, LibraryPickerTransitionReason::NotActive);
}

TEST(LibraryPickerFlow, RestoreFailureCanRetryAndStaleAcknowledgementCannotCloseRetry) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    session.generation.value = 9;
    begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    showLibrary(session);
    const auto firstCancel = flow.dispatch(CancelLibraryPicker{}, session);
    const auto* firstRestore = std::get_if<RestoreLibraryContextRequest>(&*firstCancel.request);
    ASSERT_NE(firstRestore, nullptr);
    EXPECT_EQ(firstRestore->expectedGeneration, (workshop::ContextGeneration{9}));

    EXPECT_EQ(flow.dispatch(ReplaceLibrarySelection{{model(1)}}, session).reason,
              LibraryPickerTransitionReason::RequestPending);
    EXPECT_EQ(flow.dispatch(RequestLibraryPreview{model(1)}, session).reason,
              LibraryPickerTransitionReason::RequestPending);
    EXPECT_EQ(flow.dispatch(OfferImportedLibraryItems{{model(1)}}, session).reason,
              LibraryPickerTransitionReason::RequestPending);
    EXPECT_EQ(flow.dispatch(ConfirmLibrarySelection{}, session).reason,
              LibraryPickerTransitionReason::RequestPending);
    EXPECT_EQ(flow.dispatch(CancelLibraryPicker{}, session).reason,
              LibraryPickerTransitionReason::RequestPending);

    const auto failed = flow.dispatch(CompleteLibraryRestore{firstRestore->token, false}, session);
    EXPECT_EQ(failed.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_TRUE(failed.snapshot.active);
    EXPECT_FALSE(failed.snapshot.returnPending);

    const auto retryCancel = flow.dispatch(CancelLibraryPicker{}, session);
    const auto* retryRestore = std::get_if<RestoreLibraryContextRequest>(&*retryCancel.request);
    ASSERT_NE(retryRestore, nullptr);
    EXPECT_NE(retryRestore->token, firstRestore->token);
    const auto stale = flow.dispatch(CompleteLibraryRestore{firstRestore->token, true}, session);
    EXPECT_EQ(stale.reason, LibraryPickerTransitionReason::StaleCompletion);
    EXPECT_EQ(stale.snapshot.pendingRestoreToken, retryRestore->token);

    applyRestore(session, *retryRestore);
    const auto completed = flow.dispatch(CompleteLibraryRestore{retryRestore->token, true},
                                         session);
    EXPECT_FALSE(completed.snapshot.active);
}

TEST(LibraryPickerFlow, RestoreCompletionAfterContextChangeTearsDownWithoutRetry) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    showLibrary(session);
    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);
    const auto* restore = std::get_if<RestoreLibraryContextRequest>(&*cancelled.request);
    ASSERT_NE(restore, nullptr);
    session.route = workshop::WorkshopRoute::Project;
    session.activeProject = workshop::ProjectId(44);
    session.libraryReturnRoute.reset();

    const auto completed = flow.dispatch(CompleteLibraryRestore{restore->token, false}, session);

    EXPECT_EQ(completed.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_EQ(completed.reason, LibraryPickerTransitionReason::ContextChanged);
    EXPECT_FALSE(completed.snapshot.active);
}

TEST(LibraryPickerFlow, ExactLookingLaterGenerationCannotAcknowledgeOldRestore) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    session.generation.value = 12;
    begin(flow, LibraryPickerPurpose::ManageLibrary, session);
    showLibrary(session);
    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);
    const auto* restore = std::get_if<RestoreLibraryContextRequest>(&*cancelled.request);
    ASSERT_NE(restore, nullptr);
    applyRestore(session, *restore);
    session.generation.value = restore->expectedGeneration.value + 2;

    const auto completed = flow.dispatch(CompleteLibraryRestore{restore->token, true}, session);

    EXPECT_EQ(completed.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_EQ(completed.reason, LibraryPickerTransitionReason::ContextChanged);
    EXPECT_FALSE(completed.snapshot.active);
}

TEST(LibraryPickerFlow, CancelWaitsForPendingAddExecutor) {
    LibraryPickerFlow flow;
    auto session = projectSession(41);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
    const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
    const auto* add = std::get_if<EnsureLibraryItemsInProjectRequest>(&*requested.request);
    ASSERT_NE(add, nullptr);

    const auto premature = flow.dispatch(CancelLibraryPicker{}, session);
    EXPECT_EQ(premature.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(premature.reason, LibraryPickerTransitionReason::RequestPending);
    EXPECT_TRUE(premature.snapshot.active);
    EXPECT_TRUE(premature.snapshot.pendingActionToken.has_value());
    EXPECT_FALSE(premature.request.has_value());

    flow.dispatch(CompleteLibraryAdd{add->token, workshop::ProjectId(41), {}}, session);
    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);
    EXPECT_EQ(cancelled.status, LibraryPickerTransitionStatus::RequestIssued);
    const auto* restore = std::get_if<RestoreLibraryContextRequest>(&*cancelled.request);
    ASSERT_NE(restore, nullptr);
    applyRestore(session, *restore);
    const auto completed = flow.dispatch(CompleteLibraryRestore{restore->token, true}, session);
    EXPECT_FALSE(completed.snapshot.active);
}

TEST(LibraryPickerFlow, ChangedContextCanTearDownWhileActionExecutorIsPending) {
    LibraryPickerFlow flow;
    auto session = projectSession(41);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
    flow.dispatch(ConfirmLibrarySelection{}, session);
    session.activeProject = workshop::ProjectId(42);

    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);

    EXPECT_EQ(cancelled.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_EQ(cancelled.reason, LibraryPickerTransitionReason::ContextChanged);
    EXPECT_FALSE(cancelled.snapshot.active);
    EXPECT_FALSE(cancelled.request.has_value());
}

TEST(LibraryPickerFlow, CancelWaitsForPendingStartExecutor) {
    LibraryPickerFlow flow;
    auto session = homeSession();
    begin(flow, LibraryPickerPurpose::StartProject, session);
    showLibrary(session);
    flow.dispatch(ReplaceLibrarySelection{{model(8)}}, session);
    const auto requested = flow.dispatch(ConfirmLibrarySelection{}, session);
    const auto* start = std::get_if<StartProjectWithLibraryItemRequest>(&*requested.request);
    ASSERT_NE(start, nullptr);

    const auto premature = flow.dispatch(CancelLibraryPicker{}, session);
    EXPECT_EQ(premature.status, LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(premature.reason, LibraryPickerTransitionReason::RequestPending);
    EXPECT_TRUE(premature.snapshot.active);
    EXPECT_TRUE(premature.snapshot.startRequestPending);
    EXPECT_FALSE(premature.request.has_value());

    flow.dispatch(CompleteStartProject{start->token, std::nullopt}, session);
    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);
    EXPECT_EQ(cancelled.status, LibraryPickerTransitionStatus::RequestIssued);
    const auto* restore = std::get_if<RestoreLibraryContextRequest>(&*cancelled.request);
    ASSERT_NE(restore, nullptr);
    applyRestore(session, *restore);
    const auto completed = flow.dispatch(CompleteLibraryRestore{restore->token, true}, session);
    EXPECT_FALSE(completed.snapshot.active);
}

TEST(LibraryPickerFlow, CancelWithChangedContextDeactivatesWithoutStaleRestore) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    session.activeProject = workshop::ProjectId(42);
    session.activeProjectItem.reset();

    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);

    EXPECT_EQ(cancelled.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_EQ(cancelled.reason, LibraryPickerTransitionReason::ContextChanged);
    EXPECT_FALSE(cancelled.snapshot.active);
    EXPECT_FALSE(cancelled.request.has_value());
}

TEST(LibraryPickerFlow, CancelAfterExternalNavigationDoesNotOverrideNewerRoute) {
    LibraryPickerFlow flow;
    auto session = projectSession(41, 91);
    begin(flow, LibraryPickerPurpose::AddToProject, session, "River Sign");
    showLibrary(session);
    session.route = workshop::WorkshopRoute::Home;
    session.libraryReturnRoute.reset();

    const auto cancelled = flow.dispatch(CancelLibraryPicker{}, session);

    EXPECT_EQ(cancelled.status, LibraryPickerTransitionStatus::Applied);
    EXPECT_EQ(cancelled.reason, LibraryPickerTransitionReason::ContextChanged);
    EXPECT_FALSE(cancelled.snapshot.active);
    EXPECT_FALSE(cancelled.request.has_value());
    EXPECT_EQ(session.route, workshop::WorkshopRoute::Home);
}

TEST(LibraryPickerFlow, ClosedFlowIsInertUntilExplicitlyBegun) {
    LibraryPickerFlow flow;
    auto session = homeSession();

    const auto selection = flow.dispatch(ReplaceLibrarySelection{{model(1)}}, session);
    const auto preview = flow.dispatch(RequestLibraryPreview{model(1)}, session);
    const auto confirm = flow.dispatch(ConfirmLibrarySelection{}, session);

    EXPECT_EQ(selection.reason, LibraryPickerTransitionReason::NotActive);
    EXPECT_EQ(preview.reason, LibraryPickerTransitionReason::NotActive);
    EXPECT_EQ(confirm.reason, LibraryPickerTransitionReason::NotActive);
    EXPECT_FALSE(selection.request.has_value());
    EXPECT_FALSE(preview.request.has_value());
    EXPECT_FALSE(confirm.request.has_value());
}

} // namespace
} // namespace dw::design_library
