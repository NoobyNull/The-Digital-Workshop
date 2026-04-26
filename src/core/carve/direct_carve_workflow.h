#pragma once

#include <vector>

namespace dw {
namespace carve {

enum class DirectCarveWorkflowStep {
    Model,
    Tool,
    Material,
    Preview,
    Machine,
    Zero,
    Outline,
    Confirm,
    Running
};

enum class DirectCarveRequirement {
    ModelLoaded,
    ModelFitsBlank,
    ModelFitsMachine,
    FinishingToolSelected,
    ToolSetupConfirmed,
    MaterialConfirmed,
    ToolpathGenerated,
    FreshToolpath,
    MachineConnected,
    MachineIdle,
    MachineAlarmClear,
    MachineProfileConfigured,
    MachineHomedOrSkipped,
    MachineLimitSwitchesClear,
    SafeZVerified,
    ZeroVerified,
    OutlineCompletedOrSkipped,
    FinalConfirmation
};

struct DirectCarveWorkflowState {
    bool modelLoaded = false;
    bool modelFitsBlank = false;
    bool modelFitsMachine = false;
    bool finishingToolSelected = false;
    bool toolSetupConfirmed = false;
    bool materialSelected = false;
    bool toolpathGenerated = false;
    bool toolpathFresh = false;
    bool machineConnected = false;
    bool machineIdle = false;
    bool machineAlarmClear = false;
    bool machineProfileConfigured = false;
    bool machineHomed = false;
    bool homingSkipped = false;
    bool limitSwitchesClear = false;
    bool safeZVerified = false;
    bool zeroVerified = false;
    bool outlineCompleted = false;
    bool outlineSkipped = false;
    bool finalConfirmed = false;
};

const char* directCarveRequirementLabel(DirectCarveRequirement requirement);

std::vector<DirectCarveRequirement>
missingDirectCarveRequirements(const DirectCarveWorkflowState& state,
                               bool includeFinalConfirmation);

bool isDirectCarveStepComplete(DirectCarveWorkflowStep step,
                               const DirectCarveWorkflowState& state);

bool isDirectCarveReadyForFinalConfirmation(
    const DirectCarveWorkflowState& state);

bool isDirectCarveReadyToRun(const DirectCarveWorkflowState& state);

bool canNavigateDirectCarveStep(int targetStep,
                                int maxVisitedStep,
                                int stepCount);

} // namespace carve
} // namespace dw
