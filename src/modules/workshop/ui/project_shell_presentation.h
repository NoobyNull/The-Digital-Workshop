#pragma once

#include <string>

#include "modules/workshop/project_workshop_controller.h"

namespace dw::workshop {

// What the top bar says about the open project and the machine. The words
// carry the state, so it never depends on color alone.
struct ProjectShellPresentation {
    std::string projectLabel;
    std::string machineLabel;
    bool projectDirty = false;
    bool machineRunning = false;
};

[[nodiscard]] inline ProjectShellPresentation projectShellPresentation(
    const ProjectShellSnapshot& snapshot) {
    const auto& context = snapshot.context();
    const auto& facts = snapshot.displayFacts();
    const auto& machine = snapshot.machineStatus();
    ProjectShellPresentation view;

    if (!context.activeProject)
        view.projectLabel = "No project open";
    else if (facts.projectLabel && !facts.projectLabel->empty())
        view.projectLabel = *facts.projectLabel;
    else
        view.projectLabel = "Project #" + std::to_string(context.activeProject->value);

    if (machine.running)
        view.machineLabel = machine.label.empty() ? "Machine running" : machine.label + " running";
    else if (machine.connected)
        view.machineLabel = machine.label.empty() ? "Machine ready" : machine.label + " ready";
    else
        view.machineLabel = machine.label.empty() ? "Machine offline" : machine.label + " offline";

    view.projectDirty = context.activeProject.has_value() && context.projectDirty;
    view.machineRunning = machine.running || context.runLocked();
    return view;
}

} // namespace dw::workshop
