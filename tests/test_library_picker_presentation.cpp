#include <gtest/gtest.h>

#include "modules/design_library/library_card_layout.h"
#include "modules/design_library/library_picker_presentation.h"

namespace {

using namespace dw::design_library;
namespace workshop = dw::workshop;

workshop::LibraryItemRef model(std::int64_t id) {
    return {workshop::LibraryItemKind::Model, workshop::LibraryItemId(id)};
}

workshop::LibraryItemRef gcode(std::int64_t id) {
    return {workshop::LibraryItemKind::GCode, workshop::LibraryItemId(id)};
}

TEST(LibraryPickerPresentation, InactiveSnapshotHasNoVisibleControls) {
    const auto result = makeLibraryPickerPresentation(LibraryPickerSnapshot{});

    EXPECT_FALSE(result.visible);
    EXPECT_FALSE(result.primaryVisible);
    EXPECT_FALSE(result.previewEnabled);
}

TEST(LibraryPickerPresentation, ManagePurposeExplainsExplicitPreview) {
    LibraryPickerSnapshot snapshot;
    snapshot.active = true;
    snapshot.selectedItems = {model(7)};

    const auto result = makeLibraryPickerPresentation(snapshot, "Keepsake Box");

    EXPECT_EQ(result.heading, "Design Library");
    EXPECT_NE(result.guidance.find("Preview"), std::string::npos);
    EXPECT_EQ(result.previewLabel, "Preview selected");
    EXPECT_TRUE(result.previewEnabled);
    EXPECT_FALSE(result.primaryVisible);
    EXPECT_EQ(result.cancelLabel, "Back");
}

TEST(LibraryPickerPresentation, StartPurposeRequiresOneModelAndSuggestsItsName) {
    LibraryPickerSnapshot snapshot;
    snapshot.active = true;
    snapshot.purpose = LibraryPickerPurpose::StartProject;
    snapshot.selectedItems = {model(11)};

    const auto result = makeLibraryPickerPresentation(snapshot, "  River Sign  ");

    EXPECT_EQ(result.heading, "Choose a model for your new project");
    EXPECT_EQ(result.primaryLabel, "Start project with this model");
    EXPECT_EQ(result.suggestedProjectName, "River Sign");
    EXPECT_EQ(result.selectionText, "Selected: River Sign");
    EXPECT_TRUE(result.primaryEnabled);

    snapshot.selectedItems = {gcode(12)};
    EXPECT_FALSE(makeLibraryPickerPresentation(snapshot).primaryEnabled);
}

TEST(LibraryPickerPresentation, ProjectChoiceIsSingularAndNamesTheProject) {
    LibraryPickerSnapshot snapshot;
    snapshot.active = true;
    snapshot.purpose = LibraryPickerPurpose::AddToProject;
    snapshot.activeProject = workshop::ProjectId(4);
    snapshot.activeProjectName = "River Sign";
    snapshot.selectedItems = {model(5)};

    const auto result = makeLibraryPickerPresentation(snapshot);

    EXPECT_EQ(result.heading, "Choose a model for River Sign");
    EXPECT_EQ(result.selectedCount, 1U);
    EXPECT_EQ(result.alreadyMemberCount, 0U);
    EXPECT_EQ(result.actionItemCount, 1U);
    EXPECT_EQ(result.primaryLabel, "Choose this model");
    EXPECT_EQ(result.membershipText,
              "Preview does not choose the model. Use \"Choose this model\" when ready.");
    EXPECT_TRUE(result.primaryEnabled);
}

TEST(LibraryPickerPresentation, ExistingSelectionIsClearlyIdempotent) {
    LibraryPickerSnapshot snapshot;
    snapshot.active = true;
    snapshot.purpose = LibraryPickerPurpose::AddToProject;
    snapshot.activeProject = workshop::ProjectId(4);
    snapshot.activeProjectName = "River Sign";
    snapshot.selectedItems = {model(2)};
    snapshot.projectMembership = snapshot.selectedItems;

    const auto result = makeLibraryPickerPresentation(snapshot);

    EXPECT_EQ(result.primaryLabel, "Current model");
    EXPECT_EQ(result.membershipText,
              "This is the model already chosen for River Sign.");
    EXPECT_FALSE(result.primaryEnabled);
}

TEST(LibraryPickerPresentation, ExistingModelExplainsWhyAnotherCannotBeChosen) {
    LibraryPickerSnapshot snapshot;
    snapshot.active = true;
    snapshot.purpose = LibraryPickerPurpose::AddToProject;
    snapshot.activeProject = workshop::ProjectId(4);
    snapshot.activeProjectName = "River Sign";
    snapshot.selectedItems = {model(9)};
    snapshot.projectMembership = {model(2)};

    const auto result = makeLibraryPickerPresentation(snapshot);

    EXPECT_EQ(result.membershipText,
              "River Sign already has a model. Start a new project to use this model.");
    EXPECT_FALSE(result.primaryEnabled);
}

TEST(LibraryPickerPresentation, PendingAndErrorStateAreVisibleAndDisableActions) {
    LibraryPickerSnapshot snapshot;
    snapshot.active = true;
    snapshot.purpose = LibraryPickerPurpose::AddToProject;
    snapshot.activeProject = workshop::ProjectId(4);
    snapshot.activeProjectName = "River Sign";
    snapshot.selectedItems = {model(2)};
    snapshot.pendingActionToken = LibraryPickerRequestToken{8};
    snapshot.pendingAddItems = snapshot.selectedItems;

    const auto result = makeLibraryPickerPresentation(
        snapshot, {}, "That design is linked to another project and was not deleted.");

    EXPECT_TRUE(result.actionPending);
    EXPECT_EQ(result.statusText, "Saving model...");
    EXPECT_FALSE(result.previewEnabled);
    EXPECT_FALSE(result.primaryEnabled);
    EXPECT_FALSE(result.cancelEnabled);
    EXPECT_NE(result.errorText.find("not deleted"), std::string::npos);
}

TEST(LibraryPickerPresentation, ProjectNameTrimRejectsWhitespace) {
    EXPECT_EQ(trimLibraryProjectName("  Clock Face\n"), "Clock Face");
    EXPECT_TRUE(trimLibraryProjectName(" \t\r\n ").empty());
}

TEST(LibraryPickerPresentation, ActionLayoutUsesMeasuredWidthsInsteadOfBreakpoint) {
    const std::vector<float> labels{64.0F, 180.0F, 72.0F};

    EXPECT_EQ(chooseLibraryPickerActionLayout(380.0F, labels, 8.0F, 8.0F),
              LibraryPickerActionLayout::Inline);
    EXPECT_EQ(chooseLibraryPickerActionLayout(379.0F, labels, 8.0F, 8.0F),
              LibraryPickerActionLayout::Stacked);
    EXPECT_EQ(chooseLibraryPickerActionLayout(64.0F, {64.0F}, 0.0F, 99.0F),
              LibraryPickerActionLayout::Inline);
}

TEST(LibraryCardPresentation, ReservesEveryMeasuredWrappedNameLine) {
    const auto oneLine =
        makeLibraryCardLabelLayout(96.0F, 18.0F, 18.0F, 2.0F, 1.0F, 6.0F);
    const auto twoLines =
        makeLibraryCardLabelLayout(96.0F, 36.0F, 18.0F, 2.0F, 1.0F, 6.0F);

    EXPECT_FLOAT_EQ(oneLine.wrapWidth, 96.0F);
    EXPECT_FLOAT_EQ(oneLine.labelHeight, 24.0F);
    EXPECT_FLOAT_EQ(oneLine.cellHeight, 125.0F);
    EXPECT_FLOAT_EQ(twoLines.labelHeight, 42.0F);
    EXPECT_FLOAT_EQ(twoLines.cellHeight, 143.0F);
    EXPECT_GT(twoLines.cellHeight, oneLine.cellHeight);
}

TEST(LibraryCardPresentation, UsesOneFullLineForEmptyOrZeroHeightMeasurements) {
    const auto layout =
        makeLibraryCardLabelLayout(72.0F, 0.0F, 20.0F, 3.0F, 2.0F, 5.0F);

    EXPECT_FLOAT_EQ(layout.wrapWidth, 72.0F);
    EXPECT_FLOAT_EQ(layout.labelHeight, 25.0F);
    EXPECT_FLOAT_EQ(layout.cellHeight, 105.0F);
}

TEST(LibraryPickerPresentation, DeleteResultMustConfirmUniqueRequestedItems) {
    const std::vector<workshop::LibraryItemRef> requested{model(9), gcode(9), model(10)};
    LibraryDeleteResult result{LibraryDeleteResultStatus::Deleted,
                               {model(9), gcode(9)},
                               "Deleted two items"};

    EXPECT_TRUE(isConfirmedLibraryDeletion(requested, result));

    result.confirmedItems = {model(9), model(9)};
    EXPECT_FALSE(isConfirmedLibraryDeletion(requested, result));
    result.confirmedItems = {model(99)};
    EXPECT_FALSE(isConfirmedLibraryDeletion(requested, result));
    result.confirmedItems.clear();
    EXPECT_FALSE(isConfirmedLibraryDeletion(requested, result));
    result.status = LibraryDeleteResultStatus::Blocked;
    result.confirmedItems = {model(9)};
    EXPECT_FALSE(isConfirmedLibraryDeletion(requested, result));

    result.status = LibraryDeleteResultStatus::PartiallyDeleted;
    EXPECT_TRUE(isConfirmedLibraryDeletion(requested, result));
}

} // namespace
