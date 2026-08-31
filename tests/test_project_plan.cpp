#include <gtest/gtest.h>

#include <algorithm>

#include "modules/project_plan/project_plan.h"

namespace {

using namespace dw::project_plan;
using dw::workshop::ProjectId;
using dw::workshop::ProjectItemId;
using dw::workshop::ProjectItemRef;

constexpr ProjectId kProject(7);

ProjectItemRef ref(std::int64_t item, ProjectId project = kProject) {
    return {project, ProjectItemId(item)};
}

ItemSnapshot item(std::int64_t id,
                  ItemKind kind,
                  ItemState state = ItemState::Ready,
                  std::optional<std::int64_t> parent = std::nullopt) {
    ItemSnapshot result;
    result.ref = ref(id);
    result.kind = kind;
    result.state = state;
    result.label = "Item " + std::to_string(id);
    if (parent) result.parent = ProjectItemId(*parent);
    return result;
}

OperationFacts factsFor(std::int64_t operation) {
    OperationFacts facts;
    facts.operation = ref(operation);
    return facts;
}

OperationFacts readyFacts(std::int64_t operation) {
    auto facts = factsFor(operation);
    facts.modelLoaded = Evidence::Satisfied;
    facts.modelFitsBlank = Evidence::Satisfied;
    facts.modelFitsMachine = Evidence::Satisfied;
    facts.materialSelected = Evidence::Satisfied;
    facts.blankSpecified = Evidence::Satisfied;
    facts.finishingToolSelected = Evidence::Satisfied;
    facts.toolSetupConfirmed = Evidence::Satisfied;
    facts.toolpathGenerated = Evidence::Satisfied;
    facts.toolpathFresh = Evidence::Satisfied;
    facts.machineConnected = Evidence::Satisfied;
    facts.machineIdle = Evidence::Satisfied;
    facts.machineAlarmClear = Evidence::Satisfied;
    facts.machineProfileConfigured = Evidence::Satisfied;
    facts.machineHomedOrSkipped = Evidence::Satisfied;
    facts.limitSwitchesClear = Evidence::Satisfied;
    facts.safeZVerified = Evidence::Satisfied;
    facts.zeroVerified = Evidence::Satisfied;
    facts.outlineCompletedOrSkipped = Evidence::Satisfied;
    facts.finalConfirmed = Evidence::Satisfied;
    return facts;
}

ProjectPlanInput carveInput(ItemState jobState = ItemState::Planned,
                            bool includeJob = false) {
    ProjectPlanInput input;
    input.project = kProject;
    input.projectName = "River Sign";
    input.items = {
        item(1, ItemKind::Model),
        item(2, ItemKind::Operation, ItemState::Ready, 1),
        item(3, ItemKind::Tool, ItemState::Ready, 2),
        item(4, ItemKind::Zeroing, ItemState::Ready, 2),
        item(5, ItemKind::GCode, ItemState::Ready, 2),
    };
    if (includeJob) input.items.push_back(item(6, ItemKind::Job, jobState, 5));
    input.operations.push_back(readyFacts(2));
    return input;
}

const PlanNode& node(const ProjectPlan& plan, std::int64_t id) {
    const auto found = std::find_if(plan.nodes.begin(), plan.nodes.end(), [id](const PlanNode& n) {
        return n.item.ref.item.value == id;
    });
    EXPECT_NE(found, plan.nodes.end());
    return *found;
}

std::vector<std::int64_t> raw(const std::vector<ProjectItemId>& ids) {
    std::vector<std::int64_t> result;
    for (const auto id : ids) result.push_back(id.value);
    return result;
}

TEST(ProjectPlanBuilder, DisabledBuilderEmitsNoCommand) {
    ProjectPlanBuilder builder({false});
    auto plan = builder.build(carveInput());

    EXPECT_EQ(plan.status, BuildStatus::Disabled);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::None);
    EXPECT_TRUE(plan.nodes.empty());
}

TEST(ProjectPlanBuilder, EmptyProjectStartsWithOneAddDesignAction) {
    ProjectPlanInput input;
    input.project = kProject;
    input.projectName = "New Project";

    auto plan = ProjectPlanBuilder().build(input);

    ASSERT_EQ(plan.stages.size(), 6u);
    EXPECT_EQ(plan.stages[0].title, "Design & Size");
    EXPECT_EQ(plan.stages[1].title, "Material & Blank");
    EXPECT_EQ(plan.stages[2].title, "Choose Tool");
    EXPECT_EQ(plan.stages[3].title, "Carve Preview");
    EXPECT_EQ(plan.stages[4].title, "Machine Setup");
    EXPECT_EQ(plan.stages[5].title, "Review & Run");
    EXPECT_EQ(plan.stages[0].state, StageState::Available);
    for (std::size_t index = 1; index < plan.stages.size(); ++index)
        EXPECT_EQ(plan.stages[index].state, StageState::Locked);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::AddDesignFromLibrary);
    EXPECT_EQ(plan.nextAction.label, "Choose a model");
    EXPECT_NE(plan.nextAction.explanation.find("Design Library"),
              std::string::npos);
    EXPECT_FALSE(plan.nextAction.target.has_value());
}

TEST(ProjectPlanBuilder, BuildsTrueHierarchyFromScrambledInput) {
    auto input = carveInput(ItemState::Complete, true);
    std::reverse(input.items.begin(), input.items.end());

    auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(raw(plan.roots), std::vector<std::int64_t>({1}));
    EXPECT_EQ(raw(node(plan, 1).children), std::vector<std::int64_t>({2}));
    EXPECT_EQ(raw(node(plan, 2).children),
              std::vector<std::int64_t>({3, 4, 5}));
    EXPECT_EQ(raw(node(plan, 5).children), std::vector<std::int64_t>({6}));
    EXPECT_EQ(plan.nodes.size(), 6u);
}

TEST(ProjectPlanBuilder, PartialProjectAdvancesInBeginnerStageOrder) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {item(1, ItemKind::Model),
                   item(2, ItemKind::Operation, ItemState::Planned, 1)};
    auto facts = factsFor(2);
    facts.modelLoaded = Evidence::Satisfied;
    facts.modelFitsBlank = Evidence::Satisfied;
    facts.modelFitsMachine = Evidence::Satisfied;
    input.operations = {facts};

    auto materialPlan = ProjectPlanBuilder().build(input);
    EXPECT_EQ(materialPlan.stages[0].state, StageState::Complete);
    EXPECT_EQ(materialPlan.nextAction.kind, NextActionKind::OpenMaterialAndBlank);

    input.operations[0].materialSelected = Evidence::Satisfied;
    input.operations[0].blankSpecified = Evidence::Satisfied;
    auto toolPlan = ProjectPlanBuilder().build(input);
    EXPECT_EQ(toolPlan.stages[1].state, StageState::Complete);
    EXPECT_EQ(toolPlan.nextAction.kind, NextActionKind::OpenToolSelection);

    input.operations[0].finishingToolSelected = Evidence::Satisfied;
    input.operations[0].toolSetupConfirmed = Evidence::Satisfied;
    auto previewPlan = ProjectPlanBuilder().build(input);
    EXPECT_EQ(previewPlan.stages[2].state, StageState::Complete);
    EXPECT_EQ(previewPlan.nextAction.kind, NextActionKind::OpenCarvePreview);
}

TEST(ProjectPlanBuilder, UnknownSafetyEvidenceNeverBecomesReady) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {item(1, ItemKind::Model),
                   item(2, ItemKind::Operation, ItemState::Ready, 1)};

    auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.stages[0].state, StageState::Available);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenDesignAndSize);
    EXPECT_TRUE(std::any_of(plan.stages[0].blockers.begin(),
                            plan.stages[0].blockers.end(), [](const StageBlocker& blocker) {
                                return blocker.code == BlockerCode::VerificationRequired;
                            }));
}

TEST(ProjectPlanBuilder, ReadyProjectOffersReviewAndRun) {
    auto plan = ProjectPlanBuilder().build(carveInput());

    for (std::size_t index = 0; index < 5; ++index)
        EXPECT_EQ(plan.stages[index].state, StageState::Complete) << index;
    EXPECT_EQ(plan.stages[5].state, StageState::Available);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenReviewAndRun);
    ASSERT_TRUE(plan.nextAction.target.has_value());
    EXPECT_EQ(plan.nextAction.target->item, ProjectItemId(2));
}

TEST(ProjectPlanBuilder, LiveFreshPreviewIsSendableBeforeAutomaticRunSave) {
    ProjectPlanInput input;
    input.project = kProject;
    input.projectName = "River Sign";
    input.items = {
        item(1, ItemKind::Model),
        item(2, ItemKind::Operation, ItemState::Ready, 1),
    };
    input.liveOperation = readyFacts(2);

    const auto plan = ProjectPlanBuilder().build(input);

    for (std::size_t index = 0; index < 5; ++index)
        EXPECT_EQ(plan.stages[index].state, StageState::Complete) << index;
    EXPECT_EQ(plan.stages[5].state, StageState::Available);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenReviewAndRun);
}

TEST(ProjectPlanBuilder, PersistedFreshPreviewWithoutProgramStillNeedsCarvePreview) {
    ProjectPlanInput input;
    input.project = kProject;
    input.projectName = "River Sign";
    input.items = {
        item(1, ItemKind::Model),
        item(2, ItemKind::Operation, ItemState::Ready, 1),
    };
    input.operations.push_back(readyFacts(2));

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenCarvePreview);
    EXPECT_EQ(plan.nextAction.target, ref(2));
}

TEST(ProjectPlanBuilder, StaleLivePreviewStillRequiresRegeneration) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {
        item(1, ItemKind::Model),
        item(2, ItemKind::Operation, ItemState::Ready, 1),
    };
    auto facts = readyFacts(2);
    facts.toolpathFresh = Evidence::Unsatisfied;
    input.liveOperation = facts;

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.stages[3].state, StageState::Available);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenCarvePreview);
}

TEST(ProjectPlanBuilder, ActivePreparationOutranksFocusOnAnotherBranch) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {
        item(1, ItemKind::Model),
        item(2, ItemKind::Operation, ItemState::Ready, 1),
        item(3, ItemKind::Model),
        item(4, ItemKind::Operation, ItemState::Ready, 3),
    };
    input.focusedItem = ref(4);
    input.liveOperation = readyFacts(2);

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenReviewAndRun);
    EXPECT_EQ(plan.nextAction.target, ref(2));
}

TEST(ProjectPlanBuilder, MissingEarlierItemWinsOverStaleLaterItem) {
    auto input = carveInput();
    input.items[0].state = ItemState::Missing;
    input.items[4].state = ItemState::Stale;

    auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.stages[0].state, StageState::NeedsAttention);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::RepairItem);
    ASSERT_TRUE(plan.nextAction.target.has_value());
    EXPECT_EQ(plan.nextAction.target->item, ProjectItemId(1));
    EXPECT_EQ(node(plan, 1).role, NodeRole::RepairItem);
    EXPECT_EQ(node(plan, 5).role, NodeRole::RepairItem);
}

TEST(ProjectPlanBuilder, StaleAncillaryItemDoesNotHijackPreparation) {
    auto input = carveInput();
    input.items.push_back(item(20, ItemKind::Cost, ItemState::Stale));

    auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenReviewAndRun);
    EXPECT_EQ(node(plan, 20).role, NodeRole::RepairItem);
}

TEST(ProjectPlanBuilder, LiveRunHasPriorityOverProjectWarnings) {
    auto input = carveInput(ItemState::Sent, true);
    input.items[0].state = ItemState::Stale;
    input.liveRun = LiveRunSnapshot{ref(5), ref(2), ref(6), RunState::Running};

    auto plan = ProjectPlanBuilder().build(input);

    for (std::size_t index = 0; index < 5; ++index)
        EXPECT_EQ(plan.stages[index].state, StageState::Complete) << index;
    EXPECT_EQ(plan.stages[5].state, StageState::InProgress);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::MonitorRun);
    EXPECT_EQ(plan.nextAction.target->item, ProjectItemId(6));
}

TEST(ProjectPlanBuilder, ProtectedRunPinsItsExactOperationBranch) {
    auto input = carveInput(ItemState::Sent, true);
    input.items.push_back(item(10, ItemKind::Model));
    input.items.push_back(item(11, ItemKind::Operation, ItemState::Ready, 10));
    input.focusedItem = ref(11);
    input.liveRun = LiveRunSnapshot{
        ref(5), ref(2), ref(6), RunState::Paused};

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::MonitorRun);
    ASSERT_TRUE(plan.nextAction.target);
    EXPECT_EQ(plan.nextAction.target->item, ProjectItemId(6));
    EXPECT_EQ(plan.stages[5].state, StageState::InProgress);
}

TEST(ProjectPlanBuilder, StandaloneGCodeRunIsAValidRunningBranch) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {
        item(1, ItemKind::GCode),
        item(2, ItemKind::Tool, ItemState::Ready, 1),
        item(3, ItemKind::Job, ItemState::Sent, 1),
    };
    input.liveRun = LiveRunSnapshot{
        ref(1), std::nullopt, ref(3), RunState::Running};

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::MonitorRun);
    ASSERT_TRUE(plan.nextAction.target);
    EXPECT_EQ(plan.nextAction.target->item, ProjectItemId(3));
    EXPECT_EQ(plan.stages[5].state, StageState::InProgress);
}

TEST(ProjectPlanBuilder, StandaloneGCodePausedRunRemainsMonitorable) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {
        item(1, ItemKind::GCode),
        item(3, ItemKind::Job, ItemState::Sent, 1),
    };
    input.liveRun = LiveRunSnapshot{
        ref(1), std::nullopt, ref(3), RunState::Paused};

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::MonitorRun);
    EXPECT_EQ(plan.stages[5].state, StageState::InProgress);
}

TEST(ProjectPlanBuilder, LiveRunProgramMismatchRequiresReconciliation) {
    auto input = carveInput(ItemState::Sent, true);
    input.liveRun = LiveRunSnapshot{
        ref(3), ref(2), ref(6), RunState::Running};

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::ReconcileRunState);
    EXPECT_EQ(plan.stages[5].state, StageState::NeedsAttention);
}

TEST(ProjectPlanBuilder, SecondSentJobCannotHideBehindValidLiveRun) {
    auto input = carveInput(ItemState::Sent, true);
    input.items.push_back(item(7, ItemKind::Job, ItemState::Sent, 5));
    input.liveRun = LiveRunSnapshot{
        ref(5), ref(2), ref(6), RunState::Running};

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::ReconcileRunState);
    EXPECT_EQ(plan.stages[5].state, StageState::NeedsAttention);
}

TEST(ProjectPlanBuilder, ExactInterruptedStandaloneRunIsReviewable) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {
        item(1, ItemKind::GCode),
        item(3, ItemKind::Job, ItemState::Stale, 1),
    };
    input.liveRun = LiveRunSnapshot{
        ref(1), std::nullopt, ref(3), RunState::Interrupted};

    const auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nextAction.kind, NextActionKind::ReviewInterruptedRun);
    ASSERT_TRUE(plan.nextAction.target);
    EXPECT_EQ(plan.nextAction.target->item, ProjectItemId(3));
}

TEST(ProjectPlanBuilder, SentJobWithoutMatchingLiveRunRequiresReconciliation) {
    auto plan = ProjectPlanBuilder().build(carveInput(ItemState::Sent, true));

    EXPECT_EQ(plan.stages[5].state, StageState::NeedsAttention);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::ReconcileRunState);
}

TEST(ProjectPlanBuilder, CompletedJobCompletesTheSelectedBranch) {
    auto plan = ProjectPlanBuilder().build(carveInput(ItemState::Complete, true));

    EXPECT_TRUE(plan.branchComplete);
    for (const auto& stage : plan.stages) {
        EXPECT_EQ(stage.state, StageState::Complete);
        EXPECT_TRUE(stage.blockers.empty());
    }
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::None);
}

TEST(ProjectPlanBuilder, OrphansAndCyclesArePreservedAsDiagnosticRoots) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {
        item(3, ItemKind::GCode, ItemState::Ready, 99),
        item(2, ItemKind::Model, ItemState::Ready, 1),
        item(1, ItemKind::Operation, ItemState::Ready, 2),
    };

    auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.nodes.size(), 3u);
    EXPECT_EQ(raw(plan.roots), std::vector<std::int64_t>({1, 3}));
    EXPECT_EQ(raw(node(plan, 1).children), std::vector<std::int64_t>({2}));
    EXPECT_EQ(node(plan, 1).role, NodeRole::RepairItem);
    EXPECT_EQ(node(plan, 2).role, NodeRole::RepairItem);
    EXPECT_EQ(node(plan, 3).role, NodeRole::RepairItem);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(),
                            [](const StageBlocker& issue) {
                                return issue.code == BlockerCode::ParentCycle;
                            }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(),
                            [](const StageBlocker& issue) {
                                return issue.code == BlockerCode::MissingParent;
                            }));
}

TEST(ProjectPlanBuilder, ExternalGCodeDoesNotDemandADesignWorkflow) {
    ProjectPlanInput input;
    input.project = kProject;
    input.items = {item(1, ItemKind::GCode), item(2, ItemKind::Tool, ItemState::Ready, 1)};

    auto plan = ProjectPlanBuilder().build(input);

    EXPECT_EQ(plan.stages[0].state, StageState::NotApplicable);
    EXPECT_EQ(plan.stages[1].state, StageState::NotApplicable);
    EXPECT_EQ(plan.stages[2].state, StageState::Complete);
    EXPECT_EQ(plan.stages[3].state, StageState::Complete);
    EXPECT_EQ(plan.stages[4].state, StageState::Available);
    EXPECT_EQ(plan.nextAction.kind, NextActionKind::OpenMachineSetup);
}

TEST(ProjectPlanBuilder, CanonicalOrderingIsIndependentOfInputOrder) {
    auto firstInput = carveInput();
    auto secondInput = firstInput;
    std::rotate(secondInput.items.begin(), secondInput.items.begin() + 2,
                secondInput.items.end());

    auto first = ProjectPlanBuilder().build(firstInput);
    auto second = ProjectPlanBuilder().build(secondInput);

    EXPECT_EQ(raw(first.roots), raw(second.roots));
    ASSERT_EQ(first.nodes.size(), second.nodes.size());
    for (std::size_t index = 0; index < first.nodes.size(); ++index) {
        EXPECT_EQ(first.nodes[index].item.ref, second.nodes[index].item.ref);
        EXPECT_EQ(raw(first.nodes[index].children), raw(second.nodes[index].children));
    }
    EXPECT_EQ(first.nextAction.kind, second.nextAction.kind);
}

} // namespace
