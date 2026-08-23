#include <gtest/gtest.h>

#include "modules/project_plan/project_plan_presentation.h"

namespace {

using namespace dw::project_plan;

TEST(ProjectPlanPresentation, StageStatesAlwaysHaveTextIndependentOfColor) {
    for (const auto state : {StageState::Locked,
                             StageState::Available,
                             StageState::NeedsAttention,
                             StageState::InProgress,
                             StageState::Complete,
                             StageState::NotApplicable}) {
        EXPECT_STRNE(stageStateLabel(state), "");
        EXPECT_STRNE(stageStateLabel(state), "Unknown");
    }
}

TEST(ProjectPlanPresentation, EveryNodeRoleIsExplicitlyActionOrInformation) {
    EXPECT_STREQ(nodeRoleLabel(NodeRole::ActivateItem), "Action: Open");
    EXPECT_NE(std::string(nodeRoleLabel(NodeRole::RepairItem)).find("Information"),
              std::string::npos);
    EXPECT_NE(std::string(nodeRoleLabel(NodeRole::Informational)).find("Information"),
              std::string::npos);
}

TEST(ProjectPlanPresentation, ContinueCardSuppressesNoActionAndDisabledPlans) {
    ProjectPlan plan;
    plan.status = BuildStatus::Ready;
    plan.stages[0].title = "Design & Size";
    plan.nextAction.stage = StageId::DesignAndSize;
    plan.nextAction.kind = NextActionKind::None;
    plan.nextAction.label = "Complete";
    const auto complete = buildContinueCardPresentation(plan);
    EXPECT_FALSE(complete.actionVisible);
    EXPECT_TRUE(complete.stageLabel.empty());

    plan.nextAction.kind = NextActionKind::AddDesignFromLibrary;
    const auto actionable = buildContinueCardPresentation(plan);
    EXPECT_TRUE(actionable.actionVisible);
    EXPECT_EQ(actionable.stageLabel, "Design & Size");

    plan.nextAction.kind = NextActionKind::MonitorRun;
    EXPECT_EQ(buildContinueCardPresentation(plan).stageLabel, "Run CNC");

    plan.status = BuildStatus::Disabled;
    EXPECT_FALSE(buildContinueCardPresentation(plan).actionVisible);
}

TEST(ProjectPlanPresentation, StaleAndMissingItemsNameTheirRecoveryState) {
    EXPECT_NE(std::string(itemStateLabel(ItemState::Stale)).find("refresh"),
              std::string::npos);
    EXPECT_NE(std::string(itemStateLabel(ItemState::Missing)).find("repair"),
              std::string::npos);
}

} // namespace
