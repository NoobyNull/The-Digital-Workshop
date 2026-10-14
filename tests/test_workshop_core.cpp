#include <gtest/gtest.h>

#include <optional>
#include <type_traits>

#include "modules/workshop/workshop_contract.h"
#include "modules/workshop/ui/project_context_bar_model.h"

namespace dw::workshop {

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
