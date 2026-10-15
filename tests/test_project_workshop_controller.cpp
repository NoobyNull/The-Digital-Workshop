#include <gtest/gtest.h>

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "modules/project_session/project_session.h"
#include "modules/workshop/project_workshop_controller.h"
#include "modules/workshop/ui/project_shell_presentation.h"

namespace dw::workshop {
namespace {

template <typename Payload>
WorkshopTransition send(ProjectSession& session, Payload payload) {
    return session.dispatch(
        WorkshopCommand{WorkshopCommandPayload{std::move(payload)}, std::nullopt});
}

ProjectItemRef projectItem(std::int64_t project, std::int64_t item) {
    return {ProjectId(project), ProjectItemId(item)};
}

class RecordingTarget final : public WorkshopCommandTarget {
  public:
    WorkshopContextSnapshot current;
    std::vector<WorkshopCommand> commands;

    [[nodiscard]] WorkshopContextSnapshot snapshot() const override { return current; }

    WorkshopTransition dispatch(const WorkshopCommand& command) override {
        commands.push_back(command);
        if (current.runLocked()) {
            return {TransitionStatus::Blocked, TransitionReason::ActiveRun, current};
        }
        return {TransitionStatus::Applied, TransitionReason::None, current};
    }
};

ProjectWorkshopIntent backIntent(ContextGeneration generation) {
    return BackToProjectIntent{generation};
}

ProjectWorkshopIntent returnFromLibraryIntent(ContextGeneration generation) {
    return ReturnFromLibraryIntent{generation};
}

} // namespace

static_assert(std::is_same_v<decltype(std::declval<const ProjectShellSnapshot&>().context()),
                             const WorkshopContextSnapshot&>);

TEST(ProjectWorkshopController, BackToProjectRejectsWithoutAnActiveProject) {
    ProjectSession session;
    ProjectWorkshopController controller(session);

    const auto result =
        controller.dispatch(backIntent(session.snapshot().generation));

    EXPECT_EQ(result.status, TransitionStatus::Rejected);
    EXPECT_EQ(result.reason, TransitionReason::NoActiveProject);
    EXPECT_EQ(result.context.route, WorkshopRoute::Home);
}

TEST(ProjectWorkshopController, HomeAndLibraryNavigationUseTheGuardedSessionRoute) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(4)}).accepted());
    ProjectWorkshopController controller(session);

    const auto home = controller.dispatch(ProjectWorkshopIntent{NavigateWorkshopIntent{session.snapshot().generation, WorkshopRoute::Home}});
    ASSERT_TRUE(home.accepted());
    EXPECT_EQ(home.context.route, WorkshopRoute::Home);
    EXPECT_EQ(home.context.activeProject, ProjectId(4));

    const auto library = controller.dispatch(ProjectWorkshopIntent{NavigateWorkshopIntent{session.snapshot().generation, WorkshopRoute::DesignLibrary}});
    ASSERT_TRUE(library.accepted());
    EXPECT_EQ(library.context.route, WorkshopRoute::DesignLibrary);
    EXPECT_EQ(library.context.libraryReturnRoute, WorkshopRoute::Home);
}

TEST(ProjectWorkshopController, ReturnFromLibraryRestoresHomeWithAnActiveProject) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(4)}).accepted());
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::Home}).accepted());
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(
        send(session, PreviewLibraryItem{LibraryItemRef{LibraryItemKind::Model, LibraryItemId(8)}})
            .accepted());
    ProjectWorkshopController controller(session);

    const auto result = controller.dispatch(
        returnFromLibraryIntent(session.snapshot().generation));

    EXPECT_EQ(result.status, TransitionStatus::Applied);
    EXPECT_EQ(result.context.route, WorkshopRoute::Home);
    EXPECT_EQ(result.context.activeProject, ProjectId(4));
    EXPECT_FALSE(result.context.libraryPreview.has_value());
    EXPECT_FALSE(result.context.libraryReturnRoute.has_value());
}

TEST(ProjectWorkshopController, ReturnFromLibraryRestoresTheExactProjectItem) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(4)}).accepted());
    ASSERT_TRUE(send(session, SelectProjectItem{projectItem(4, 19)}).accepted());
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(
        send(session, PreviewLibraryItem{LibraryItemRef{LibraryItemKind::Model, LibraryItemId(8)}})
            .accepted());
    ProjectWorkshopController controller(session);

    const auto result = controller.dispatch(
        returnFromLibraryIntent(session.snapshot().generation));

    EXPECT_EQ(result.status, TransitionStatus::Applied);
    EXPECT_EQ(result.context.route, WorkshopRoute::Project);
    EXPECT_EQ(result.context.origin, SelectionOrigin::ProjectItem);
    ASSERT_TRUE(result.context.activeProjectItem.has_value());
    EXPECT_EQ(*result.context.activeProjectItem, projectItem(4, 19));
    EXPECT_FALSE(result.context.libraryPreview.has_value());
    EXPECT_FALSE(result.context.libraryReturnRoute.has_value());
}

TEST(ProjectWorkshopController, ReturnFromLibraryRejectsAStaleGeneration) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(4)}).accepted());
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    const ContextGeneration staleGeneration = session.snapshot().generation;
    ASSERT_TRUE(
        send(session, PreviewLibraryItem{LibraryItemRef{LibraryItemKind::Model, LibraryItemId(8)}})
            .accepted());
    const ContextGeneration currentGeneration = session.snapshot().generation;
    ProjectWorkshopController controller(session);

    const auto result =
        controller.dispatch(returnFromLibraryIntent(staleGeneration));

    EXPECT_EQ(result.status, TransitionStatus::Rejected);
    EXPECT_EQ(result.reason, TransitionReason::StaleGeneration);
    EXPECT_EQ(result.context.route, WorkshopRoute::DesignLibrary);
    EXPECT_EQ(result.context.generation, currentGeneration);
    ASSERT_TRUE(result.context.libraryPreview.has_value());
    EXPECT_EQ(result.context.libraryPreview->item, LibraryItemId(8));
}

TEST(ProjectWorkshopController, ActiveRunBlocksReturnFromLibraryCommand) {
    RecordingTarget target;
    target.current.activeProject = ProjectId(4);
    target.current.route = WorkshopRoute::DesignLibrary;
    target.current.libraryReturnRoute = WorkshopRoute::Home;
    target.current.activeRun = RunLockRef{RunId(3), std::nullopt};
    target.current.generation = ContextGeneration{9};
    ProjectWorkshopController controller(target);

    const auto result = controller.dispatch(
        returnFromLibraryIntent(ContextGeneration{9}));

    EXPECT_EQ(result.status, TransitionStatus::Blocked);
    EXPECT_EQ(result.reason, TransitionReason::ActiveRun);
    ASSERT_EQ(target.commands.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<ReturnFromLibrary>(target.commands.front().payload));
    EXPECT_EQ(target.commands.front().expectedGeneration, ContextGeneration{9});
}

TEST(ProjectWorkshopController, BackFromLibraryRestoresTheProjectSelection) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(4)}).accepted());
    ASSERT_TRUE(send(session, SelectProjectItem{projectItem(4, 19)}).accepted());
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_TRUE(
        send(session, PreviewLibraryItem{LibraryItemRef{LibraryItemKind::Model, LibraryItemId(8)}})
            .accepted());
    ProjectWorkshopController controller(session);

    const auto result =
        controller.dispatch(backIntent(session.snapshot().generation));

    EXPECT_EQ(result.status, TransitionStatus::Applied);
    EXPECT_EQ(result.context.route, WorkshopRoute::Project);
    EXPECT_EQ(result.context.origin, SelectionOrigin::ProjectItem);
    ASSERT_TRUE(result.context.activeProjectItem.has_value());
    EXPECT_EQ(result.context.activeProjectItem->project, ProjectId(4));
    EXPECT_EQ(result.context.activeProjectItem->item, ProjectItemId(19));
    EXPECT_FALSE(result.context.libraryPreview.has_value());
    EXPECT_FALSE(result.context.libraryReturnRoute.has_value());
}

TEST(ProjectWorkshopController, BackFromHomeAlwaysOpensTheActiveProject) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(4)}).accepted());
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::Home}).accepted());
    ProjectWorkshopController controller(session);

    const auto result =
        controller.dispatch(backIntent(session.snapshot().generation));

    EXPECT_EQ(result.status, TransitionStatus::Applied);
    EXPECT_EQ(result.context.route, WorkshopRoute::Project);
    EXPECT_EQ(result.context.activeProject, ProjectId(4));
}

TEST(ProjectWorkshopController, BackToProjectIssuesOneNavigateCommand) {
    RecordingTarget target;
    target.current.activeProject = ProjectId(2);
    target.current.route = WorkshopRoute::DesignLibrary;
    target.current.libraryReturnRoute = WorkshopRoute::Project;
    target.current.generation = ContextGeneration{9};
    ProjectWorkshopController controller(target);

    const auto result = controller.dispatch(backIntent(ContextGeneration{9}));

    EXPECT_TRUE(result.accepted());
    ASSERT_EQ(target.commands.size(), 1U);
    const auto* navigate = std::get_if<NavigateTo>(&target.commands.front().payload);
    ASSERT_NE(navigate, nullptr);
    EXPECT_EQ(navigate->route, WorkshopRoute::Project);
    EXPECT_EQ(target.commands.front().expectedGeneration, ContextGeneration{9});
}

TEST(ProjectWorkshopController, SnapshotPreservesAllCallerAndSessionFacts) {
    RecordingTarget target;
    target.current.activeProject = ProjectId(7);
    target.current.activeProjectItem = projectItem(7, 31);
    target.current.libraryPreview = LibraryItemRef{LibraryItemKind::GCode, LibraryItemId(17)};
    target.current.activeRun = RunLockRef{RunId(5), projectItem(7, 31)};
    target.current.libraryReturnRoute = WorkshopRoute::Project;
    target.current.runReturnRoute = WorkshopRoute::DesignLibrary;
    target.current.route = WorkshopRoute::RunCnc;
    target.current.origin = SelectionOrigin::ProjectItem;
    target.current.generation = ContextGeneration{42};
    target.current.projectDirty = true;
    target.current.preparationLocked = true;
    const ProjectDisplayFacts display{
        std::string{"Walnut sign"},
        std::string{"Front relief"},
        std::string{"Library preview"},
    };
    const MachineStatusSnapshot machine{"Running pass 2", true, true};
    ProjectWorkshopController controller(target);

    const ProjectShellSnapshot shell = controller.snapshot(display, machine);
    const auto& context = shell.context();

    EXPECT_EQ(context.activeProject, target.current.activeProject);
    ASSERT_TRUE(context.activeProjectItem.has_value());
    EXPECT_EQ(context.activeProjectItem->project, ProjectId(7));
    EXPECT_EQ(context.activeProjectItem->item, ProjectItemId(31));
    ASSERT_TRUE(context.libraryPreview.has_value());
    EXPECT_EQ(context.libraryPreview->kind, LibraryItemKind::GCode);
    EXPECT_EQ(context.libraryPreview->item, LibraryItemId(17));
    ASSERT_TRUE(context.activeRun.has_value());
    EXPECT_EQ(context.activeRun->run, RunId(5));
    EXPECT_EQ(context.libraryReturnRoute, WorkshopRoute::Project);
    EXPECT_EQ(context.runReturnRoute, WorkshopRoute::DesignLibrary);
    EXPECT_EQ(context.route, WorkshopRoute::RunCnc);
    EXPECT_EQ(context.origin, SelectionOrigin::ProjectItem);
    EXPECT_EQ(context.generation, ContextGeneration{42});
    EXPECT_TRUE(context.projectDirty);
    EXPECT_TRUE(context.preparationLocked);
    EXPECT_EQ(shell.displayFacts().projectLabel, display.projectLabel);
    EXPECT_EQ(shell.displayFacts().itemLabel, display.itemLabel);
    EXPECT_EQ(shell.displayFacts().previewLabel, display.previewLabel);
    EXPECT_EQ(shell.machineStatus().label, machine.label);
    EXPECT_EQ(shell.machineStatus().connected, machine.connected);
    EXPECT_EQ(shell.machineStatus().running, machine.running);
}

TEST(ProjectWorkshopController, ShellPresentationNamesTheProjectAndMachineInWords) {
    RecordingTarget target;
    ProjectWorkshopController controller(target);
    ProjectDisplayFacts display;
    MachineStatusSnapshot machine{"Virtual CNC", true, false};

    auto view = projectShellPresentation(controller.snapshot(display, machine));
    EXPECT_EQ(view.projectLabel, "No project open");
    EXPECT_EQ(view.machineLabel, "Virtual CNC ready");
    EXPECT_FALSE(view.projectDirty);
    EXPECT_FALSE(view.machineRunning);

    target.current.activeProject = ProjectId(7);
    target.current.projectDirty = true;
    view = projectShellPresentation(controller.snapshot(display, machine));
    EXPECT_EQ(view.projectLabel, "Project #7");
    EXPECT_TRUE(view.projectDirty);

    display.projectLabel = "Walnut sign";
    machine.running = true;
    view = projectShellPresentation(controller.snapshot(display, machine));
    EXPECT_EQ(view.projectLabel, "Walnut sign");
    EXPECT_EQ(view.machineLabel, "Virtual CNC running");
    EXPECT_TRUE(view.machineRunning);

    machine = MachineStatusSnapshot{"", false, false};
    target.current.activeRun = RunLockRef{RunId(11), std::nullopt};
    view = projectShellPresentation(controller.snapshot(display, machine));
    EXPECT_EQ(view.machineLabel, "Machine offline");
    EXPECT_TRUE(view.machineRunning);
}

TEST(ProjectWorkshopController, BackIntentCarriesTheSnapshotGenerationGuard) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(12)}).accepted());
    const ContextGeneration staleGeneration = session.snapshot().generation;
    ASSERT_TRUE(send(session, NavigateTo{WorkshopRoute::Home}).accepted());
    const ContextGeneration currentGeneration = session.snapshot().generation;
    ProjectWorkshopController controller(session);

    const auto result = controller.dispatch(backIntent(staleGeneration));

    EXPECT_EQ(result.status, TransitionStatus::Rejected);
    EXPECT_EQ(result.reason, TransitionReason::StaleGeneration);
    EXPECT_EQ(result.context.route, WorkshopRoute::Home);
    EXPECT_EQ(result.context.generation, currentGeneration);
}

TEST(ProjectWorkshopController, ItemSelectionUsesSessionOwnershipAndGeneration) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(41)}).accepted());
    ProjectWorkshopController controller(session);

    const auto selected = controller.dispatch(ProjectWorkshopIntent{SelectProjectItemIntent{session.snapshot().generation, projectItem(41, 7)}});

    EXPECT_EQ(selected.status, TransitionStatus::Applied);
    ASSERT_TRUE(selected.context.activeProjectItem.has_value());
    EXPECT_EQ(selected.context.activeProjectItem->item, ProjectItemId(7));

    const auto mismatched = controller.dispatch(ProjectWorkshopIntent{SelectProjectItemIntent{session.snapshot().generation, projectItem(99, 8)}});

    EXPECT_EQ(mismatched.status, TransitionStatus::Rejected);
    EXPECT_EQ(mismatched.reason, TransitionReason::ProjectMismatch);
    ASSERT_TRUE(mismatched.context.activeProjectItem.has_value());
    EXPECT_EQ(mismatched.context.activeProjectItem->project, ProjectId(41));
    EXPECT_EQ(mismatched.context.activeProjectItem->item, ProjectItemId(7));
}

TEST(ProjectWorkshopController, LibraryPreviewIsAnOverlayAndBackReturnsToProject) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(41)}).accepted());
    ASSERT_TRUE(send(session, SelectProjectItem{projectItem(41, 7)}).accepted());
    ProjectWorkshopController controller(session);

    const auto previewed = controller.dispatch(ProjectWorkshopIntent{
        PreviewLibraryItemIntent{session.snapshot().generation,
                                 LibraryItemRef{LibraryItemKind::Model, LibraryItemId(12)}}});

    EXPECT_EQ(previewed.status, TransitionStatus::Applied);
    EXPECT_EQ(previewed.context.route, WorkshopRoute::DesignLibrary);
    ASSERT_TRUE(previewed.context.libraryPreview.has_value());
    EXPECT_EQ(previewed.context.libraryPreview->item, LibraryItemId(12));
    ASSERT_TRUE(previewed.context.activeProjectItem.has_value());
    EXPECT_EQ(previewed.context.activeProjectItem->item, ProjectItemId(7));

    const auto returned =
        controller.dispatch(backIntent(session.snapshot().generation));
    EXPECT_EQ(returned.status, TransitionStatus::Applied);
    EXPECT_EQ(returned.context.route, WorkshopRoute::Project);
    EXPECT_FALSE(returned.context.libraryPreview.has_value());
    ASSERT_TRUE(returned.context.activeProjectItem.has_value());
    EXPECT_EQ(returned.context.activeProjectItem->item, ProjectItemId(7));
}

TEST(ProjectWorkshopController, ClearItemIntentUsesTheSameGuardedSessionPath) {
    ProjectSession session;
    ASSERT_TRUE(send(session, ActivateProject{ProjectId(41)}).accepted());
    ASSERT_TRUE(send(session, SelectProjectItem{projectItem(41, 7)}).accepted());
    ProjectWorkshopController controller(session);

    const auto cleared = controller.dispatch(ProjectWorkshopIntent{
        ClearProjectItemIntent{session.snapshot().generation}});

    EXPECT_EQ(cleared.status, TransitionStatus::Applied);
    EXPECT_FALSE(cleared.context.activeProjectItem.has_value());
    EXPECT_EQ(cleared.context.route, WorkshopRoute::Project);
}

} // namespace dw::workshop
