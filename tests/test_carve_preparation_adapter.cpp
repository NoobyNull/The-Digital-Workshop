#include <gtest/gtest.h>

#include "app/carve_preparation_adapter.h"

namespace {

using namespace dw;

ProjectOpenItem model(i64 id, i64 sourceId, i64 project = 7) {
    ProjectOpenItem item;
    item.id = id;
    item.projectId = project;
    item.itemType = ProjectOpenItemType::Model;
    item.sourceTable = "models";
    item.sourceId = sourceId;
    item.status = ProjectOpenItemStatus::Ready;
    return item;
}

ProjectOpenItem operation(i64 id, i64 parent, i64 project = 7) {
    ProjectOpenItem item;
    item.id = id;
    item.projectId = project;
    item.itemType = ProjectOpenItemType::Operation;
    item.parentItemId = parent;
    item.status = ProjectOpenItemStatus::Planned;
    item.intentJson = R"({"operation_kind":"direct_carve","model_name":"Sign"})";
    return item;
}

constexpr workshop::ProjectId kProject{7};
constexpr carve_preparation::PreparationToken kToken{3};
constexpr carve_preparation::PreparationRevision kRevision{4};

} // namespace

TEST(CarvePreparationAdapter, NoProjectReturnsOnlyExplicitCreateRequirement) {
    const auto result = resolvePrepareCarvePin(
        std::nullopt, {}, kToken, kRevision, {});
    EXPECT_EQ(result.status, PrepareCarveAdapterStatus::CreateProjectRequired);
    EXPECT_FALSE(result.pin.has_value());
    EXPECT_EQ(result.issue, carve_preparation::PreparationIdentityIssue::NoActiveProject);
}

TEST(CarvePreparationAdapter, ModelWithoutOperationRequiresExplicitCreation) {
    const auto result = resolvePrepareCarvePin(
        kProject,
        {kProject, workshop::ProjectItemId(10)},
        kToken,
        kRevision,
        {model(10, 91)});
    EXPECT_EQ(result.status, PrepareCarveAdapterStatus::OperationRequired);
    EXPECT_FALSE(result.pin.has_value());
}

TEST(CarvePreparationAdapter, ExactOperationPinsProjectModelSourceAndParent) {
    const auto result = resolvePrepareCarvePin(
        kProject,
        {kProject, workshop::ProjectItemId(11)},
        kToken,
        kRevision,
        {operation(11, 10), model(10, 91)});
    ASSERT_EQ(result.status, PrepareCarveAdapterStatus::Ready);
    ASSERT_TRUE(result.pin.has_value());
    EXPECT_EQ(result.pin->project(), kProject);
    EXPECT_EQ(result.pin->modelItem().item, workshop::ProjectItemId(10));
    EXPECT_EQ(result.pin->modelSource().item, workshop::LibraryItemId(91));
    EXPECT_EQ(result.pin->operationItem().item, workshop::ProjectItemId(11));
}

TEST(CarvePreparationAdapter, ForeignStaleAndMalformedTargetsNeverProducePin) {
    auto stale = operation(11, 10);
    stale.status = ProjectOpenItemStatus::Stale;
    EXPECT_EQ(resolvePrepareCarvePin(
                  kProject,
                  {kProject, workshop::ProjectItemId(11)},
                  kToken,
                  kRevision,
                  {model(10, 91), stale})
                  .status,
              PrepareCarveAdapterStatus::TargetUnavailable);

    auto wrongParent = operation(11, 99);
    EXPECT_EQ(resolvePrepareCarvePin(
                  kProject,
                  {kProject, workshop::ProjectItemId(11)},
                  kToken,
                  kRevision,
                  {model(10, 91), wrongParent})
                  .status,
              PrepareCarveAdapterStatus::InvalidHierarchy);

    EXPECT_EQ(resolvePrepareCarvePin(
                  kProject,
                  {workshop::ProjectId(8), workshop::ProjectItemId(11)},
                  kToken,
                  kRevision,
                  {model(10, 91), operation(11, 10)})
                  .status,
              PrepareCarveAdapterStatus::TargetUnavailable);
}

TEST(CarvePreparationAdapter, MultipleDirectCarveChildrenAreNeverChosenImplicitly) {
    const auto result = resolvePrepareCarvePin(
        kProject,
        {kProject, workshop::ProjectItemId(10)},
        kToken,
        kRevision,
        {model(10, 91), operation(11, 10), operation(12, 10)});
    EXPECT_EQ(result.status, PrepareCarveAdapterStatus::AmbiguousOperation);
    EXPECT_FALSE(result.pin.has_value());
}
