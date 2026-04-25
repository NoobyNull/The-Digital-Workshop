#include "direct_carve_workflow.h"

namespace dw {
namespace carve {

const char* directCarveRequirementLabel(DirectCarveRequirement requirement)
{
    switch (requirement) {
    case DirectCarveRequirement::ModelLoaded:
        return "Model loaded";
    case DirectCarveRequirement::ModelFitsBlank:
        return "Model fits material blank";
    case DirectCarveRequirement::ModelFitsMachine:
        return "Model fits machine travel";
    case DirectCarveRequirement::FinishingToolSelected:
        return "Finishing tool selected";
    case DirectCarveRequirement::ToolSetupConfirmed:
        return "Required tool setup confirmed";
    case DirectCarveRequirement::MaterialConfirmed:
        return "Material and feeds confirmed";
    case DirectCarveRequirement::HeightmapReady:
        return "Heightmap ready";
    case DirectCarveRequirement::FreshHeightmap:
        return "Heightmap matches current model setup";
    case DirectCarveRequirement::ToolpathGenerated:
        return "Toolpath generated";
    case DirectCarveRequirement::FreshToolpath:
        return "Toolpath matches current settings";
    case DirectCarveRequirement::MachineConnected:
        return "CNC connected";
    case DirectCarveRequirement::MachineIdle:
        return "Machine idle";
    case DirectCarveRequirement::MachineAlarmClear:
        return "Machine not in alarm or unknown state";
    case DirectCarveRequirement::MachineProfileConfigured:
        return "Machine profile configured";
    case DirectCarveRequirement::MachineHomedOrSkipped:
        return "Machine homed or explicitly skipped";
    case DirectCarveRequirement::MachineLimitSwitchesClear:
        return "Limit switches clear";
    case DirectCarveRequirement::SafeZVerified:
        return "Safe Z verified";
    case DirectCarveRequirement::ZeroVerified:
        return "Work zero verified";
    case DirectCarveRequirement::OutlineCompletedOrSkipped:
        return "Outline completed or explicitly skipped";
    case DirectCarveRequirement::FinalConfirmation:
        return "Final confirmation checked";
    }
    return "Unknown requirement";
}

std::vector<DirectCarveRequirement>
missingDirectCarveRequirements(const DirectCarveWorkflowState& state,
                               bool includeFinalConfirmation)
{
    std::vector<DirectCarveRequirement> missing;
    auto require = [&](bool ok, DirectCarveRequirement requirement) {
        if (!ok) missing.push_back(requirement);
    };

    require(state.modelLoaded, DirectCarveRequirement::ModelLoaded);
    require(state.modelFitsBlank, DirectCarveRequirement::ModelFitsBlank);
    require(state.modelFitsMachine, DirectCarveRequirement::ModelFitsMachine);
    require(state.finishingToolSelected,
            DirectCarveRequirement::FinishingToolSelected);
    require(state.toolSetupConfirmed,
            DirectCarveRequirement::ToolSetupConfirmed);
    require(state.materialSelected, DirectCarveRequirement::MaterialConfirmed);
    require(state.heightmapReady, DirectCarveRequirement::HeightmapReady);
    require(state.heightmapFresh, DirectCarveRequirement::FreshHeightmap);
    require(state.toolpathGenerated, DirectCarveRequirement::ToolpathGenerated);
    require(state.toolpathFresh, DirectCarveRequirement::FreshToolpath);
    require(state.machineConnected, DirectCarveRequirement::MachineConnected);
    require(state.machineIdle, DirectCarveRequirement::MachineIdle);
    require(state.machineAlarmClear, DirectCarveRequirement::MachineAlarmClear);
    require(state.machineProfileConfigured,
            DirectCarveRequirement::MachineProfileConfigured);
    require(state.machineHomed || state.homingSkipped,
            DirectCarveRequirement::MachineHomedOrSkipped);
    require(state.limitSwitchesClear,
            DirectCarveRequirement::MachineLimitSwitchesClear);
    require(state.safeZVerified, DirectCarveRequirement::SafeZVerified);
    require(state.zeroVerified, DirectCarveRequirement::ZeroVerified);
    require(state.outlineCompleted || state.outlineSkipped,
            DirectCarveRequirement::OutlineCompletedOrSkipped);
    if (includeFinalConfirmation) {
        require(state.finalConfirmed,
                DirectCarveRequirement::FinalConfirmation);
    }

    return missing;
}

bool isDirectCarveStepComplete(DirectCarveWorkflowStep step,
                               const DirectCarveWorkflowState& state)
{
    switch (step) {
    case DirectCarveWorkflowStep::Model:
        return state.modelLoaded && state.modelFitsBlank &&
               state.modelFitsMachine;
    case DirectCarveWorkflowStep::Tool:
        return state.finishingToolSelected && state.toolSetupConfirmed;
    case DirectCarveWorkflowStep::Material:
        return state.materialSelected;
    case DirectCarveWorkflowStep::Preview:
        return state.heightmapReady && state.heightmapFresh &&
               state.toolpathGenerated && state.toolpathFresh;
    case DirectCarveWorkflowStep::Machine:
        return state.machineConnected && state.machineIdle &&
               state.machineAlarmClear && state.machineProfileConfigured &&
               (state.machineHomed || state.homingSkipped) &&
               state.limitSwitchesClear &&
               state.safeZVerified;
    case DirectCarveWorkflowStep::Zero:
        return state.zeroVerified;
    case DirectCarveWorkflowStep::Outline:
        return state.outlineCompleted || state.outlineSkipped;
    case DirectCarveWorkflowStep::Confirm:
        return isDirectCarveReadyToRun(state);
    case DirectCarveWorkflowStep::Running:
        return false;
    }
    return false;
}

bool isDirectCarveReadyForFinalConfirmation(
    const DirectCarveWorkflowState& state)
{
    return missingDirectCarveRequirements(state, false).empty();
}

bool isDirectCarveReadyToRun(const DirectCarveWorkflowState& state)
{
    return missingDirectCarveRequirements(state, true).empty();
}

} // namespace carve
} // namespace dw
