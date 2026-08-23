#include <gtest/gtest.h>

#include <optional>
#include <type_traits>

#include "modules/workshop/experience_router.h"
#include "modules/workshop/ui/project_context_bar_model.h"

namespace dw::workshop {
namespace {

class FakeCommandTarget final : public WorkshopCommandTarget {
  public:
    WorkshopContextSnapshot current;
    int dispatchCount = 0;
    std::optional<WorkshopCommand> lastCommand;

    [[nodiscard]] WorkshopContextSnapshot snapshot() const override {
        return current;
    }

    WorkshopTransition dispatch(const WorkshopCommand& command) override {
        ++dispatchCount;
        lastCommand = command;

        if (const auto* activate = std::get_if<ActivateProject>(&command.payload)) {
            current.activeProject = activate->project;
            current.route = WorkshopRoute::Project;
            ++current.generation.value;
        }

        return {
            TransitionStatus::Applied,
            TransitionReason::None,
            current,
        };
    }
};

WorkshopCommand activateProject(std::int64_t id,
                                std::optional<ContextGeneration> expected = std::nullopt) {
    return {ActivateProject{ProjectId(id)}, expected};
}

} // namespace

static_assert(!std::is_same_v<ProjectId, ProjectItemId>);
static_assert(!std::is_same_v<ProjectId, LibraryItemId>);

TEST(WorkshopCore, StrongIdsAndReferencesRejectInvalidDefaults) {
    const ProjectId missingProject;
    const ProjectId project(42);
    const ProjectItemRef missingItem;
    const ProjectItemRef item{project, ProjectItemId(9)};
    const LibraryItemRef libraryItem{LibraryItemKind::Model, LibraryItemId(7)};

    EXPECT_FALSE(missingProject.valid());
    EXPECT_TRUE(project.valid());
    EXPECT_EQ(project.value, 42);
    EXPECT_FALSE(missingItem.valid());
    EXPECT_TRUE(item.valid());
    EXPECT_TRUE(libraryItem.valid());
    EXPECT_EQ(item.project, project);
}

TEST(WorkshopCore, DefaultContextHasOneNeutralProjectTruth) {
    const WorkshopContextSnapshot context;

    EXPECT_EQ(context.route, WorkshopRoute::Home);
    EXPECT_EQ(context.origin, SelectionOrigin::None);
    EXPECT_FALSE(context.activeProject.has_value());
    EXPECT_FALSE(context.activeProjectItem.has_value());
    EXPECT_FALSE(context.libraryPreview.has_value());
    EXPECT_FALSE(context.libraryReturnRoute.has_value());
    EXPECT_FALSE(context.runReturnRoute.has_value());
    EXPECT_EQ(context.generation.value, 0U);
    EXPECT_FALSE(context.projectDirty);
    EXPECT_FALSE(context.preparationLocked);
    EXPECT_FALSE(context.runLocked());
}

TEST(WorkshopCore, LibraryPreviewOverlaysRatherThanReplacesProjectSelection) {
    WorkshopContextSnapshot context;
    context.activeProject = ProjectId(3);
    context.activeProjectItem = ProjectItemRef{ProjectId(3), ProjectItemId(11)};
    context.libraryPreview = LibraryItemRef{LibraryItemKind::Model, LibraryItemId(99)};
    context.libraryReturnRoute = WorkshopRoute::Project;
    context.route = WorkshopRoute::DesignLibrary;
    context.origin = SelectionOrigin::LibraryPreview;

    ASSERT_TRUE(context.activeProjectItem.has_value());
    ASSERT_TRUE(context.libraryPreview.has_value());
    EXPECT_EQ(context.activeProjectItem->item, ProjectItemId(11));
    EXPECT_EQ(context.libraryPreview->item, LibraryItemId(99));
    EXPECT_EQ(context.libraryReturnRoute, WorkshopRoute::Project);
}

TEST(WorkshopCore, GuidedEnabledForwardsOriginalCommandExactlyOnce) {
    FakeCommandTarget target;
    ExperienceRouter router(target, true);
    const auto command = activateProject(12, ContextGeneration{4});

    const auto result = router.dispatch(ExperienceMode::Guided, command);

    EXPECT_TRUE(result.accepted());
    EXPECT_EQ(target.dispatchCount, 1);
    ASSERT_TRUE(target.lastCommand.has_value());
    EXPECT_EQ(target.lastCommand->expectedGeneration, ContextGeneration{4});
    const auto* activate = std::get_if<ActivateProject>(&target.lastCommand->payload);
    ASSERT_NE(activate, nullptr);
    EXPECT_EQ(activate->project, ProjectId(12));
}

TEST(WorkshopCore, GuidedDisabledRejectsWithoutDispatchOrFallback) {
    FakeCommandTarget target;
    target.current.activeProject = ProjectId(6);
    target.current.generation = ContextGeneration{8};
    ExperienceRouter router(target, false);

    const auto result = router.dispatch(ExperienceMode::Guided, activateProject(12));

    EXPECT_FALSE(result.accepted());
    EXPECT_EQ(result.status, TransitionStatus::Rejected);
    EXPECT_EQ(result.reason, TransitionReason::ExperienceDisabled);
    EXPECT_EQ(result.context.activeProject, ProjectId(6));
    EXPECT_EQ(result.context.generation, ContextGeneration{8});
    EXPECT_EQ(target.dispatchCount, 0);
}

TEST(WorkshopCore, AdvancedForwardsThroughSameTargetWhenGuidedIsDisabled) {
    FakeCommandTarget target;
    ExperienceRouter router(target, false);

    const auto result = router.dispatch(ExperienceMode::Advanced, activateProject(21));

    EXPECT_TRUE(result.accepted());
    EXPECT_EQ(target.dispatchCount, 1);
    EXPECT_EQ(result.context.activeProject, ProjectId(21));
    EXPECT_EQ(result.context.generation, ContextGeneration{1});
}

TEST(WorkshopCore, ReenablingGuidedDoesNotMutateSharedContext) {
    FakeCommandTarget target;
    target.current.activeProject = ProjectId(5);
    target.current.generation = ContextGeneration{3};
    ExperienceRouter router(target, false);

    router.setGuidedEnabled(true);

    EXPECT_TRUE(router.guidedEnabled());
    EXPECT_EQ(target.dispatchCount, 0);
    EXPECT_EQ(target.current.activeProject, ProjectId(5));
    EXPECT_EQ(target.current.generation, ContextGeneration{3});

    const auto result = router.dispatch(ExperienceMode::Guided, activateProject(8));
    EXPECT_TRUE(result.accepted());
    EXPECT_EQ(target.dispatchCount, 1);
    EXPECT_EQ(result.context.activeProject, ProjectId(8));
}

TEST(WorkshopCore, GuidedAndAdvancedObserveOneSharedGeneration) {
    FakeCommandTarget target;
    ExperienceRouter router(target, true);

    const auto guided = router.dispatch(ExperienceMode::Guided, activateProject(2));
    const auto advanced = router.dispatch(ExperienceMode::Advanced, activateProject(7));

    EXPECT_EQ(guided.context.generation, ContextGeneration{1});
    EXPECT_EQ(advanced.context.generation, ContextGeneration{2});
    EXPECT_EQ(target.current.activeProject, ProjectId(7));
    EXPECT_EQ(target.dispatchCount, 2);
}

TEST(WorkshopCore, ContextBarStacksBeforeMeasuredColumnsCanOverlap) {
    EXPECT_TRUE(projectContextBarUsesTwoRows(1334.0F,
                                             350.0F,
                                             700.0F,
                                             260.0F,
                                             16.0F));
    EXPECT_FALSE(projectContextBarUsesTwoRows(3840.0F,
                                              350.0F,
                                              700.0F,
                                              260.0F,
                                              16.0F));
}

TEST(WorkshopCore, ContextBarKeepsOneRowWhenMeasuredContentExactlyFits) {
    EXPECT_FALSE(projectContextBarUsesTwoRows(1032.0F,
                                              300.0F,
                                              400.0F,
                                              300.0F,
                                              16.0F));
    EXPECT_TRUE(projectContextBarUsesTwoRows(0.0F, 0.0F, 0.0F, 0.0F, 0.0F));
}

TEST(WorkshopCore, ContextBarOneRowReservesEdgesAndGivesFocusTheRemainder) {
    const auto columns = projectContextBarOneRowColumns(
        1334.0F, 150.0F, 180.0F, 16.0F);

    EXPECT_FLOAT_EQ(columns.project, 150.0F);
    EXPECT_FLOAT_EQ(columns.action, 180.0F);
    EXPECT_FLOAT_EQ(columns.context, 972.0F);
    EXPECT_FALSE(projectContextBarUsesTwoRows(
        1334.0F, 150.0F, 700.0F, 180.0F, 16.0F));
    EXPECT_TRUE(projectContextBarUsesTwoRows(
        900.0F, 150.0F, 700.0F, 180.0F, 16.0F));
}

} // namespace dw::workshop
