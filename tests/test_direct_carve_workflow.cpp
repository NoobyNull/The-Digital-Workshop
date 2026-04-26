#include <gtest/gtest.h>

#include "core/carve/direct_carve_workflow.h"

#include <algorithm>

namespace {

dw::carve::DirectCarveWorkflowState readyState()
{
    dw::carve::DirectCarveWorkflowState state;
    state.modelLoaded = true;
    state.modelFitsBlank = true;
    state.modelFitsMachine = true;
    state.finishingToolSelected = true;
    state.toolSetupConfirmed = true;
    state.materialSelected = true;
    state.toolpathGenerated = true;
    state.toolpathFresh = true;
    state.machineConnected = true;
    state.machineIdle = true;
    state.machineAlarmClear = true;
    state.machineProfileConfigured = true;
    state.machineHomed = true;
    state.limitSwitchesClear = true;
    state.safeZVerified = true;
    state.zeroVerified = true;
    state.outlineCompleted = true;
    state.finalConfirmed = true;
    return state;
}

bool missingContains(const std::vector<dw::carve::DirectCarveRequirement>& missing,
                     dw::carve::DirectCarveRequirement requirement)
{
    return std::find(missing.begin(), missing.end(), requirement) != missing.end();
}

} // namespace

TEST(DirectCarveWorkflow, ReadyStateAllowsFinalCarve)
{
    EXPECT_TRUE(dw::carve::isDirectCarveReadyToRun(readyState()));
}

TEST(DirectCarveWorkflow, StaleToolpathBlocksFinalCarve)
{
    auto state = readyState();
    state.toolpathFresh = false;

    const auto missing = dw::carve::missingDirectCarveRequirements(state, true);

    EXPECT_FALSE(dw::carve::isDirectCarveReadyToRun(state));
    EXPECT_TRUE(missingContains(
        missing, dw::carve::DirectCarveRequirement::FreshToolpath));
}

TEST(DirectCarveWorkflow, OutlineSkipSatisfiesOutlineRequirement)
{
    auto state = readyState();
    state.outlineCompleted = false;
    state.outlineSkipped = true;

    EXPECT_TRUE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Outline, state));
    EXPECT_TRUE(dw::carve::isDirectCarveReadyToRun(state));
}

TEST(DirectCarveWorkflow, MachineGateRequiresExplicitMachineReadiness)
{
    auto state = readyState();
    state.machineProfileConfigured = false;
    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Machine, state));

    state = readyState();
    state.machineIdle = false;
    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Machine, state));

    state = readyState();
    state.safeZVerified = false;
    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Machine, state));
}

TEST(DirectCarveWorkflow, MachineGateRequiresHomingOrExplicitSkip)
{
    auto state = readyState();
    state.machineHomed = false;
    state.homingSkipped = false;

    const auto missing = dw::carve::missingDirectCarveRequirements(state, true);

    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Machine, state));
    EXPECT_FALSE(dw::carve::isDirectCarveReadyToRun(state));
    EXPECT_TRUE(missingContains(
        missing, dw::carve::DirectCarveRequirement::MachineHomedOrSkipped));

    state.homingSkipped = true;

    EXPECT_TRUE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Machine, state));
    EXPECT_TRUE(dw::carve::isDirectCarveReadyToRun(state));
}

TEST(DirectCarveWorkflow, MachineGateRequiresClearLimitSwitches)
{
    auto state = readyState();
    state.limitSwitchesClear = false;

    const auto missing = dw::carve::missingDirectCarveRequirements(state, true);

    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Machine, state));
    EXPECT_FALSE(dw::carve::isDirectCarveReadyToRun(state));
    EXPECT_TRUE(missingContains(
        missing, dw::carve::DirectCarveRequirement::MachineLimitSwitchesClear));
}

TEST(DirectCarveWorkflow, ToolGateRequiresToolSetupConfirmation)
{
    auto state = readyState();
    state.toolSetupConfirmed = false;

    const auto missing = dw::carve::missingDirectCarveRequirements(state, true);

    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Tool, state));
    EXPECT_FALSE(dw::carve::isDirectCarveReadyToRun(state));
    EXPECT_TRUE(missingContains(
        missing, dw::carve::DirectCarveRequirement::ToolSetupConfirmed));
}

TEST(DirectCarveWorkflow, FinalConfirmationAloneIsNotEnough)
{
    auto state = readyState();
    state.modelLoaded = false;

    EXPECT_FALSE(dw::carve::isDirectCarveReadyToRun(state));
    EXPECT_FALSE(dw::carve::isDirectCarveStepComplete(
        dw::carve::DirectCarveWorkflowStep::Confirm, state));
}

TEST(DirectCarveWorkflow, StepIndicatorAllowsImmediateNextStep)
{
    constexpr int stepCount = 9;
    constexpr int zeroStep = 5;
    constexpr int outlineStep = 6;
    constexpr int confirmStep = 7;

    EXPECT_TRUE(dw::carve::canNavigateDirectCarveStep(
        outlineStep, zeroStep, stepCount));
    EXPECT_TRUE(dw::carve::canNavigateDirectCarveStep(
        2, zeroStep, stepCount));
    EXPECT_FALSE(dw::carve::canNavigateDirectCarveStep(
        confirmStep, zeroStep, stepCount));
    EXPECT_FALSE(dw::carve::canNavigateDirectCarveStep(
        -1, zeroStep, stepCount));
    EXPECT_FALSE(dw::carve::canNavigateDirectCarveStep(
        stepCount, zeroStep, stepCount));
}
