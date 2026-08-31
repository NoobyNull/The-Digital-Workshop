#pragma once

#include <string>

#include "modules/workshop/project_workshop_controller.h"

namespace dw::workshop {

struct ProjectContextBarPresentation {
    std::string projectLabel;
    std::string stageLabel;
    std::string focusLabel;
    std::string machineLabel;
    bool projectDirty = false;
    bool showBackToProject = false;
    bool enableBackToProject = false;
    bool runLocked = false;
};

struct ProjectContextBarOneRowColumns {
    float project = 0.0F;
    float context = 0.0F;
    float action = 0.0F;
};

// Preserve the measured edge content and give every remaining pixel to the
// variable-length context label. This prevents proportional columns from
// clipping a long focus label even when all measured content fits on one row.
[[nodiscard]] inline ProjectContextBarOneRowColumns projectContextBarOneRowColumns(
    float availableWidth,
    float projectWidth,
    float actionWidth,
    float columnGutter) noexcept {
    ProjectContextBarOneRowColumns result;
    result.project = projectWidth > 0.0F ? projectWidth : 0.0F;
    result.action = actionWidth > 0.0F ? actionWidth : 0.0F;
    const float remaining = availableWidth - result.project - result.action -
                            2.0F * (columnGutter > 0.0F ? columnGutter : 0.0F);
    result.context = remaining > 0.0F ? remaining : 0.0F;
    return result;
}

// UI-free responsive policy. The renderer supplies measured text/widget widths
// so high UI scales cannot make adjacent context columns overlap.
[[nodiscard]] inline bool projectContextBarUsesTwoRows(float availableWidth,
                                                       float projectWidth,
                                                       float contextWidth,
                                                       float actionWidth,
                                                       float columnGutter) noexcept {
    if (availableWidth <= 0.0F)
        return true;
    return projectContextBarOneRowColumns(
               availableWidth, projectWidth, actionWidth, columnGutter)
               .context < contextWidth;
}

[[nodiscard]] inline const char* projectContextBarStageLabel(WorkshopRoute route) noexcept {
    switch (route) {
    case WorkshopRoute::Home:
        return "Home";
    case WorkshopRoute::Project:
        return "Project";
    case WorkshopRoute::DesignLibrary:
        return "Design Library";
    case WorkshopRoute::RunCnc:
        return "Run CNC";
    }
    return "Workshop";
}

[[nodiscard]] inline ProjectContextBarPresentation projectContextBarPresentation(
    const ProjectShellSnapshot& snapshot) {
    const auto& context = snapshot.context();
    const auto& facts = snapshot.displayFacts();
    const auto& machine = snapshot.machineStatus();
    ProjectContextBarPresentation view;

    if (!context.activeProject)
        view.projectLabel = "No project open";
    else if (facts.projectLabel && !facts.projectLabel->empty())
        view.projectLabel = *facts.projectLabel;
    else
        view.projectLabel = "Project #" + std::to_string(context.activeProject->value);

    view.stageLabel = projectContextBarStageLabel(context.route);
    if (context.libraryPreview) {
        const std::string item = facts.previewLabel.value_or(
            "Design #" + std::to_string(context.libraryPreview->item.value));
        view.focusLabel = "Library preview  /  " + item;
    } else if (context.activeProjectItem) {
        const std::string item = facts.itemLabel.value_or(
            "Item #" + std::to_string(context.activeProjectItem->item.value));
        view.focusLabel = "Project item  /  " + item;
    } else if (context.activeProject) {
        view.focusLabel = "Overview";
    } else {
        view.focusLabel = "Start or open a project to keep your work together";
    }

    if (machine.running)
        view.machineLabel = machine.label.empty() ? "Machine running" : machine.label + " running";
    else if (machine.connected)
        view.machineLabel = machine.label.empty() ? "Machine ready" : machine.label + " ready";
    else
        view.machineLabel = machine.label.empty() ? "Machine offline" : machine.label + " offline";

    view.projectDirty = context.projectDirty;
    view.showBackToProject = context.activeProject.has_value();
    view.runLocked = machine.running || context.runLocked();
    view.enableBackToProject = view.showBackToProject && !view.runLocked &&
                               (context.route != WorkshopRoute::Project ||
                                context.libraryPreview.has_value());
    return view;
}

} // namespace dw::workshop
