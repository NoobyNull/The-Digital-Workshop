#include <gtest/gtest.h>

#include <optional>
#include <variant>
#include <vector>

#include "modules/carve_preparation/preparation_identity.h"

namespace {

using namespace dw;
using namespace dw::carve_preparation;

constexpr workshop::ProjectId kProject{17};
constexpr workshop::ProjectId kOtherProject{18};
constexpr workshop::ProjectItemRef kModel{kProject, workshop::ProjectItemId{101}};
constexpr workshop::ProjectItemRef kOperation{kProject, workshop::ProjectItemId{102}};
constexpr workshop::LibraryItemRef kModelSource{workshop::LibraryItemKind::Model,
                                                workshop::LibraryItemId{501}};
constexpr PreparationToken kToken{41};
constexpr PreparationRevision kRevision{7};

PrepareCarvePin makePin(workshop::ProjectId project = kProject,
                        workshop::ProjectItemRef model = kModel,
                        workshop::LibraryItemRef source = kModelSource,
                        workshop::ProjectItemRef operation = kOperation,
                        PreparationToken token = kToken,
                        PreparationRevision revision = kRevision) {
    return {project, model, source, operation, token, revision};
}

std::vector<PreparationItemSnapshot> matchingItems() {
    return {
        PreparationItemSnapshot{kModel, PreparationItemKind::Model, std::nullopt, kModelSource},
        PreparationItemSnapshot{
            kOperation, PreparationItemKind::Operation, kModel.item, std::nullopt},
    };
}

PreparationIdentitySnapshot matchingSnapshot() {
    return {kProject, kRevision, matchingItems()};
}

} // namespace

TEST(PreparationIdentity, ExactSnapshotEmitsTheExactPinnedBeginCommand) {
    const auto pin = makePin();
    const auto snapshot = matchingSnapshot();

    const auto result = PreparationIdentityPolicy::evaluate(true, pin, snapshot);

    ASSERT_EQ(result.status, PreparationIdentityStatus::Ready);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::None);
    ASSERT_TRUE(result.command.has_value());
    const auto* begin = std::get_if<BeginPinnedPreparation>(&*result.command);
    ASSERT_NE(begin, nullptr);
    EXPECT_TRUE(begin->pin == pin);
    EXPECT_EQ(begin->pin.project(), kProject);
    EXPECT_EQ(begin->pin.modelItem(), kModel);
    EXPECT_EQ(begin->pin.operationItem(), kOperation);
    EXPECT_EQ(begin->pin.token(), kToken);
    EXPECT_EQ(begin->pin.revision(), kRevision);
}

TEST(PreparationIdentity, DisabledModeIsInertBeforeAnyIdentityInspection) {
    const PreparationIdentitySnapshot noProject{std::nullopt, kRevision, {}};

    const auto result = PreparationIdentityPolicy::evaluate(false, std::nullopt, noProject);

    EXPECT_EQ(result.status, PreparationIdentityStatus::Disabled);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::ModuleDisabled);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, NoActiveProjectEmitsOnlyExplicitCreationIntent) {
    const PreparationIdentitySnapshot noProject{std::nullopt, kRevision, {}};

    const auto result = PreparationIdentityPolicy::evaluate(true, std::nullopt, noProject);

    EXPECT_EQ(result.status, PreparationIdentityStatus::CreateProjectRequired);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::NoActiveProject);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_NE(std::get_if<RequestProjectCreation>(&*result.command), nullptr);
    EXPECT_EQ(std::get_if<BeginPinnedPreparation>(&*result.command), nullptr);
}

TEST(PreparationIdentity, CrossProjectModelIsInvalidAndEmitsNothing) {
    const workshop::ProjectItemRef foreignModel{kOtherProject, workshop::ProjectItemId{101}};
    const auto pin = makePin(kProject, foreignModel);

    const auto result = PreparationIdentityPolicy::evaluate(true, pin, matchingSnapshot());

    EXPECT_EQ(result.status, PreparationIdentityStatus::InvalidIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::CrossProjectModel);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, CrossProjectOperationIsInvalidAndEmitsNothing) {
    const workshop::ProjectItemRef foreignOperation{kOtherProject, workshop::ProjectItemId{102}};
    const auto pin = makePin(kProject, kModel, kModelSource, foreignOperation);

    const auto result = PreparationIdentityPolicy::evaluate(true, pin, matchingSnapshot());

    EXPECT_EQ(result.status, PreparationIdentityStatus::InvalidIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::CrossProjectOperation);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, GCodeCannotBeUsedAsThePinnedModelSource) {
    const workshop::LibraryItemRef gcodeSource{workshop::LibraryItemKind::GCode,
                                               workshop::LibraryItemId{501}};
    const auto pin = makePin(kProject, kModel, gcodeSource);

    const auto result = PreparationIdentityPolicy::evaluate(true, pin, matchingSnapshot());

    EXPECT_EQ(result.status, PreparationIdentityStatus::InvalidIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::InvalidModelSource);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, ChangedActiveProjectMakesAnOtherwiseValidPinStale) {
    const PreparationIdentitySnapshot switched{kOtherProject, kRevision, {}};

    const auto result = PreparationIdentityPolicy::evaluate(true, makePin(), switched);

    EXPECT_EQ(result.status, PreparationIdentityStatus::StaleIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::ActiveProjectChanged);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, ChangedRevisionMakesThePinStaleBeforeItemUse) {
    const PreparationIdentitySnapshot revised{kProject, PreparationRevision{8}, matchingItems()};

    const auto result = PreparationIdentityPolicy::evaluate(true, makePin(), revised);

    EXPECT_EQ(result.status, PreparationIdentityStatus::StaleIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::RevisionChanged);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, MissingPinnedModelIsStaleAndEmitsNothing) {
    const PreparationIdentitySnapshot withoutModel{
        kProject,
        kRevision,
        {PreparationItemSnapshot{kOperation, PreparationItemKind::Operation, kModel.item}}};

    const auto result = PreparationIdentityPolicy::evaluate(true, makePin(), withoutModel);

    EXPECT_EQ(result.status, PreparationIdentityStatus::StaleIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::ModelMissing);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, ChangedModelSourceMakesThePinStale) {
    const workshop::LibraryItemRef replacement{workshop::LibraryItemKind::Model,
                                               workshop::LibraryItemId{777}};
    auto items = matchingItems();
    items[0] =
        PreparationItemSnapshot{kModel, PreparationItemKind::Model, std::nullopt, replacement};
    const PreparationIdentitySnapshot changed{kProject, kRevision, items};

    const auto result = PreparationIdentityPolicy::evaluate(true, makePin(), changed);

    EXPECT_EQ(result.status, PreparationIdentityStatus::StaleIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::ModelSourceChanged);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, OperationMustBeAnExactChildOfThePinnedModel) {
    auto items = matchingItems();
    items[1] = PreparationItemSnapshot{kOperation,
                                       PreparationItemKind::Operation,
                                       workshop::ProjectItemId{999}};
    const PreparationIdentitySnapshot wrongParent{kProject, kRevision, items};

    const auto result = PreparationIdentityPolicy::evaluate(true, makePin(), wrongParent);

    EXPECT_EQ(result.status, PreparationIdentityStatus::InvalidIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::OperationParentMismatch);
    EXPECT_FALSE(result.command.has_value());
}

TEST(PreparationIdentity, AmbiguousSnapshotIdentityIsRejected) {
    auto items = matchingItems();
    items.push_back(items.front());
    const PreparationIdentitySnapshot duplicate{kProject, kRevision, items};

    const auto result = PreparationIdentityPolicy::evaluate(true, makePin(), duplicate);

    EXPECT_EQ(result.status, PreparationIdentityStatus::InvalidIdentity);
    EXPECT_EQ(result.issue, PreparationIdentityIssue::DuplicateSnapshotItem);
    EXPECT_FALSE(result.command.has_value());
}
