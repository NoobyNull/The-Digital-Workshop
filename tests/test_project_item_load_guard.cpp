#include <gtest/gtest.h>

#include "modules/workshop/project_item_load_guard.h"

namespace dw::workshop {
namespace {

TEST(ProjectItemLoadGuard, CommitsOnlyForTheExactVisibleProjectItem) {
    const ProjectItemRef expected{ProjectId(4), ProjectItemId(9)};
    WorkshopContextSnapshot context;
    context.activeProject = expected.project;
    context.activeProjectItem = expected;
    context.route = WorkshopRoute::Project;

    EXPECT_TRUE(projectItemLoadCanCommit(context, expected));

    context.activeProjectItem = ProjectItemRef{ProjectId(4), ProjectItemId(10)};
    EXPECT_FALSE(projectItemLoadCanCommit(context, expected));

    context.activeProjectItem = expected;
    context.route = WorkshopRoute::DesignLibrary;
    context.libraryPreview = LibraryItemRef{LibraryItemKind::Model, LibraryItemId(12)};
    EXPECT_FALSE(projectItemLoadCanCommit(context, expected));

    context.route = WorkshopRoute::Home;
    context.libraryPreview.reset();
    EXPECT_FALSE(projectItemLoadCanCommit(context, expected));
}

TEST(ProjectItemLoadGuard, LibraryLoadCommitsOnlyForTheExactVisiblePreview) {
    const LibraryItemRef expected{LibraryItemKind::Model, LibraryItemId(12)};
    WorkshopContextSnapshot context;
    context.route = WorkshopRoute::DesignLibrary;
    context.libraryPreview = expected;

    EXPECT_TRUE(libraryPreviewLoadCanCommit(context, expected));

    context.libraryPreview = LibraryItemRef{LibraryItemKind::Model, LibraryItemId(13)};
    EXPECT_FALSE(libraryPreviewLoadCanCommit(context, expected));

    context.libraryPreview = expected;
    context.route = WorkshopRoute::Project;
    EXPECT_FALSE(libraryPreviewLoadCanCommit(context, expected));

    context.route = WorkshopRoute::Home;
    context.libraryPreview.reset();
    EXPECT_FALSE(libraryPreviewLoadCanCommit(context, expected));
}

} // namespace
} // namespace dw::workshop
