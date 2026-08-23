#include <gtest/gtest.h>

#include "app/project_plan_input_adapter.h"

namespace {

using namespace dw;
using namespace dw::project_plan;

ProjectOpenItem item(i64 id,
                     ProjectOpenItemType type,
                     ProjectOpenItemStatus status = ProjectOpenItemStatus::Ready,
                     std::optional<i64> parent = std::nullopt) {
    ProjectOpenItem result;
    result.id = id;
    result.projectId = 9;
    result.itemType = type;
    result.status = status;
    result.parentItemId = parent;
    result.displayName = "Item " + std::to_string(id);
    return result;
}

const OperationFacts& operation(const ProjectPlanInput& input) {
    EXPECT_EQ(input.operations.size(), 1u);
    return input.operations.front();
}

TEST(ProjectPlanInputAdapter, MapsEveryOpenItemKindAndState) {
    const std::vector<ProjectOpenItemType> types = {
        ProjectOpenItemType::Model,      ProjectOpenItemType::Material,
        ProjectOpenItemType::Stock,      ProjectOpenItemType::Tool,
        ProjectOpenItemType::Operation,  ProjectOpenItemType::Gcode,
        ProjectOpenItemType::CutPlan,    ProjectOpenItemType::Cost,
        ProjectOpenItemType::Job,        ProjectOpenItemType::Labor,
        ProjectOpenItemType::Consumable, ProjectOpenItemType::Zeroing,
    };
    for (std::size_t index = 0; index < types.size(); ++index)
        EXPECT_EQ(static_cast<std::size_t>(toProjectPlanItemKind(types[index])), index);

    const std::vector<ProjectOpenItemStatus> states = {
        ProjectOpenItemStatus::Planned,  ProjectOpenItemStatus::Ready,
        ProjectOpenItemStatus::Generated, ProjectOpenItemStatus::Sent,
        ProjectOpenItemStatus::Complete, ProjectOpenItemStatus::Stale,
        ProjectOpenItemStatus::Missing,
    };
    for (std::size_t index = 0; index < states.size(); ++index)
        EXPECT_EQ(static_cast<std::size_t>(toProjectPlanItemState(states[index])), index);
}

TEST(ProjectPlanInputAdapter, PreservesIdentityParentAndFocusedItem) {
    auto model = item(1, ProjectOpenItemType::Model);
    auto operationItem = item(2, ProjectOpenItemType::Operation,
                              ProjectOpenItemStatus::Ready, 1);
    const workshop::ProjectItemRef focused{workshop::ProjectId(9),
                                           workshop::ProjectItemId(2)};

    auto input = makeProjectPlanInput(workshop::ProjectId(9), "River Sign",
                                      {operationItem, model}, focused);

    EXPECT_EQ(input.projectName, "River Sign");
    ASSERT_EQ(input.items.size(), 2u);
    EXPECT_EQ(input.items[0].ref.item, workshop::ProjectItemId(2));
    EXPECT_EQ(input.items[0].parent, workshop::ProjectItemId(1));
    EXPECT_EQ(input.focusedItem, focused);
}

TEST(ProjectPlanInputAdapter, DecodesOnlyPersistedEvidenceThatIsActuallyKnown) {
    auto operationItem = item(2, ProjectOpenItemType::Operation);
    operationItem.intentJson = R"({
        "operation_kind":"direct_carve",
        "stock":{"width_mm":200,"height_mm":100,"thickness_mm":18}
    })";
    operationItem.snapshotJson = R"({
        "setup": {
            "model_loaded": true,
            "material_selected": true,
            "finishing_tool_selected": true,
            "toolpath_generated": true,
            "settings_version": 4,
            "generated_at_version": 4
        },
        "machine": {
            "max_travel_x_mm": 800,
            "max_travel_y_mm": 800,
            "max_travel_z_mm": 100
        }
    })";
    auto zero = item(3, ProjectOpenItemType::Zeroing,
                     ProjectOpenItemStatus::Ready, 2);
    zero.snapshotJson = R"({"zero_verified":true})";

    auto input = makeProjectPlanInput(workshop::ProjectId(9), "River Sign",
                                      {operationItem, zero});
    const auto& facts = operation(input);

    EXPECT_EQ(facts.modelLoaded, Evidence::Satisfied);
    EXPECT_EQ(facts.materialSelected, Evidence::Satisfied);
    EXPECT_EQ(facts.blankSpecified, Evidence::Satisfied);
    EXPECT_EQ(facts.finishingToolSelected, Evidence::Satisfied);
    EXPECT_EQ(facts.toolpathGenerated, Evidence::Satisfied);
    EXPECT_EQ(facts.toolpathFresh, Evidence::Satisfied);
    EXPECT_EQ(facts.machineProfileConfigured, Evidence::Unknown);
    EXPECT_EQ(facts.zeroVerified, Evidence::Satisfied);
    EXPECT_EQ(facts.modelFitsBlank, Evidence::Unknown);
    EXPECT_EQ(facts.machineConnected, Evidence::Unknown);
    EXPECT_EQ(facts.toolSetupConfirmed, Evidence::Unknown);
}

TEST(ProjectPlanInputAdapter, ChangedToolpathVersionAndFalseSetupStayUnsatisfied) {
    auto operationItem = item(2, ProjectOpenItemType::Operation);
    operationItem.intentJson = R"({"operation_kind":"direct_carve"})";
    operationItem.snapshotJson = R"({"setup":{
        "material_selected":false,
        "toolpath_generated":true,
        "settings_version":5,
        "generated_at_version":4
    }})";

    const auto input = makeProjectPlanInput(
        workshop::ProjectId(9), "Project", {operationItem});
    const auto& facts = operation(input);
    EXPECT_EQ(facts.materialSelected, Evidence::Unsatisfied);
    EXPECT_EQ(facts.blankSpecified, Evidence::Unknown);
    EXPECT_EQ(facts.toolpathFresh, Evidence::Unsatisfied);
}

TEST(ProjectPlanInputAdapter, BlankDimensionsAreIndependentOfMaterialSelection) {
    auto operationItem = item(2, ProjectOpenItemType::Operation);
    operationItem.intentJson = R"({
        "operation_kind":"direct_carve",
        "stock":{"width_mm":200,"height_mm":100,"thickness_mm":18}
    })";
    operationItem.snapshotJson = R"({"setup":{"material_selected":false}})";

    const auto input = makeProjectPlanInput(
        workshop::ProjectId(9), "Project", {operationItem});
    const auto& facts = operation(input);
    EXPECT_EQ(facts.materialSelected, Evidence::Unsatisfied);
    EXPECT_EQ(facts.blankSpecified, Evidence::Satisfied);
}

TEST(ProjectPlanInputAdapter, CompleteNonPositiveBlankIsUnsatisfied) {
    auto operationItem = item(2, ProjectOpenItemType::Operation);
    operationItem.intentJson = R"({
        "operation_kind":"direct_carve",
        "stock":{"width_mm":200,"height_mm":0,"thickness_mm":18}
    })";

    const auto input = makeProjectPlanInput(
        workshop::ProjectId(9), "Project", {operationItem});
    EXPECT_EQ(operation(input).blankSpecified, Evidence::Unsatisfied);
}

TEST(ProjectPlanInputAdapter, IgnoresSetupFactsFromAnotherOperationSchema) {
    auto operationItem = item(2, ProjectOpenItemType::Operation);
    operationItem.intentJson = R"({
        "operation_kind":"laser_engrave",
        "stock":{"width_mm":200,"height_mm":100,"thickness_mm":18}
    })";
    operationItem.snapshotJson = R"({"setup":{
        "model_loaded":true,
        "material_selected":true,
        "finishing_tool_selected":true,
        "toolpath_generated":true
    }})";

    const auto input = makeProjectPlanInput(
        workshop::ProjectId(9), "Project", {operationItem});
    const auto& facts = operation(input);
    EXPECT_EQ(facts.modelLoaded, Evidence::Unknown);
    EXPECT_EQ(facts.materialSelected, Evidence::Unknown);
    EXPECT_EQ(facts.blankSpecified, Evidence::Unknown);
    EXPECT_EQ(facts.finishingToolSelected, Evidence::Unknown);
    EXPECT_EQ(facts.toolpathGenerated, Evidence::Unknown);
}

TEST(ProjectPlanInputAdapter, InvalidJsonRemainsUnknownInsteadOfReady) {
    auto operationItem = item(2, ProjectOpenItemType::Operation);
    operationItem.intentJson = "not-json";
    operationItem.snapshotJson = "[not-an-object]";

    const auto input = makeProjectPlanInput(
        workshop::ProjectId(9), "Project", {operationItem});
    const auto& facts = operation(input);
    EXPECT_EQ(facts.materialSelected, Evidence::Unknown);
    EXPECT_EQ(facts.toolpathFresh, Evidence::Unknown);
    EXPECT_EQ(facts.machineProfileConfigured, Evidence::Unknown);
}

TEST(ProjectPlanInputAdapter, FocusRequiresExactProjectItemMembership) {
    auto model = item(1, ProjectOpenItemType::Model);
    const workshop::ProjectItemRef missing{workshop::ProjectId(9),
                                           workshop::ProjectItemId(99)};
    const auto input = makeProjectPlanInput(workshop::ProjectId(9), "Project",
                                            {model}, missing);
    EXPECT_FALSE(input.focusedItem.has_value());
    EXPECT_FALSE(input.liveRun.has_value());
}

TEST(ProjectPlanInputAdapter, LiveFactsCarryIdentityAndBlankOnly) {
    const carve_preparation::PrepareCarvePin pin(
        workshop::ProjectId(9),
        {workshop::ProjectId(9), workshop::ProjectItemId(1)},
        {workshop::LibraryItemKind::Model, workshop::LibraryItemId(41)},
        {workshop::ProjectId(9), workshop::ProjectItemId(2)},
        carve_preparation::PreparationToken{7},
        carve_preparation::PreparationRevision{8});

    const auto facts = makeLiveProjectPlanOperationFacts(pin, true);

    EXPECT_EQ(facts.operation, pin.operationItem());
    EXPECT_EQ(facts.blankSpecified, Evidence::Satisfied);
    // Carve-stage evidence is absent until the CAM rebuild supplies it.
    EXPECT_EQ(facts.modelLoaded, Evidence::Unknown);
    EXPECT_EQ(facts.materialSelected, Evidence::Unknown);
    EXPECT_EQ(facts.toolpathGenerated, Evidence::Unknown);
    EXPECT_EQ(facts.toolpathFresh, Evidence::Unknown);
    EXPECT_EQ(facts.machineHomedOrSkipped, Evidence::Unknown);
    EXPECT_EQ(facts.zeroVerified, Evidence::Unknown);
    EXPECT_EQ(facts.finalConfirmed, Evidence::Unknown);

    const auto blocked = makeLiveProjectPlanOperationFacts(pin, false);
    EXPECT_EQ(blocked.blankSpecified, Evidence::Unsatisfied);
}

} // namespace
