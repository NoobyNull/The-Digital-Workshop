#include <gtest/gtest.h>

#include "modules/viewport/viewport_presentation.h"

namespace viewport = dw::viewport;

TEST(ViewportPresentation, NoneDoesNotRenderAnIdentityOverlay) {
    const auto view = viewport::presentIdentity(viewport::PresentationIdentity::none());

    EXPECT_FALSE(view.visible);
    EXPECT_TRUE(view.badge.empty());
    EXPECT_TRUE(view.label.empty());
}

TEST(ViewportPresentation, ProjectItemNamesBothProjectAndItem) {
    const auto identity =
        viewport::PresentationIdentity::projectItem("River sign", "Roughing pass");

    const auto view = viewport::presentIdentity(identity);

    ASSERT_TRUE(view.visible);
    EXPECT_EQ(identity.kind(), viewport::ContentIdentityKind::ProjectItem);
    EXPECT_EQ(view.badge, "Project item");
    EXPECT_EQ(view.label, "River sign  /  Roughing pass");
    EXPECT_EQ(view.accessibleText, "Project item: River sign  /  Roughing pass");
}

TEST(ViewportPresentation, LibraryPreviewIsExplicitlyDifferentFromProjectContent) {
    const auto identity =
        viewport::PresentationIdentity::libraryPreview("River sign", "Leaf flourish");

    const auto view = viewport::presentIdentity(identity);

    ASSERT_TRUE(view.visible);
    EXPECT_EQ(identity.kind(), viewport::ContentIdentityKind::LibraryPreview);
    EXPECT_EQ(view.badge, "Library preview");
    EXPECT_EQ(view.label, "River sign  /  Leaf flourish");
    EXPECT_EQ(view.accessibleText, "Library preview: River sign  /  Leaf flourish");
}

TEST(ViewportPresentation, EmptyItemDoesNotInventVisibleContent) {
    const auto project =
        viewport::PresentationIdentity::projectItem("River sign", "");
    const auto preview =
        viewport::PresentationIdentity::libraryPreview("River sign", "");

    EXPECT_FALSE(viewport::presentIdentity(project).visible);
    EXPECT_FALSE(viewport::presentIdentity(preview).visible);
}
