// Application composition for the project-centered workshop shell. This file
// owns UI intent wiring only; ProjectSession remains the policy authority.

#include "app/application.h"
#include "app/direct_carve_run_effect_adapter.h"
#include "app/library_workflow_coordinator.h"
#include "app/project_plan_input_adapter.h"
#include "app/project_plan_run_truth_adapter.h"

#include <cstdint>
#include <string>
#include <utility>

#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "core/database/cut_plan_repository.h"
#include "core/database/gcode_repository.h"
#include "core/library/library_manager.h"
#include "core/materials/material_manager.h"
#include "core/optimizer/cut_list_file.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/project_session/project_session.h"
#include "modules/project_plan/project_plan.h"
#include "modules/workshop/project_item_load_guard.h"
#include "modules/workshop/project_resume.h"
#include "modules/workshop/project_workshop_controller.h"
#include "ui/panels/cost_panel.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/start_page.h"
#include "ui/widgets/toast.h"

namespace dw {
void Application::wireWorkshop() {
    wireStartPage();
    wireLibraryPanel();
    wireProjectPanel();

    m_uiManager->setOnBackToProject([this]() {
        if (m_libraryWorkflow && m_libraryWorkflow->picker().snapshot().active) {
            std::string error;
            (void)requestLibraryReturn(error);
            return;
        }
        if (!m_projectSession || !m_projectWorkshopController)
            return;
        const bool machineActionActive =
            m_cncController && m_cncController->isStreaming();
        if (machineActionActive) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Run CNC Is Active",
                "Pause or stop the active run before returning to the project.");
            return;
        }
        const auto context = m_projectSession->snapshot();
        const auto experience = m_projectWorkshopController->guidedEnabled()
                                    ? workshop::ExperienceMode::Guided
                                    : workshop::ExperienceMode::Advanced;
        const auto transition = m_projectWorkshopController->dispatch(
            workshop::BackToProjectIntent{experience, context.generation});
        if (!transition.accepted()) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Project Navigation Blocked",
                transition.reason == workshop::TransitionReason::ActiveRun
                    ? "Finish or stop the active run before leaving Run CNC."
                    : "The project context changed. Try Back to Project again.");
            return;
        }
        m_uiManager->showStartPage() = false;
        m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
        m_uiManager->openWindow("project");
    });
}

void Application::refreshProjectShell() {
    if (!m_projectSession || !m_projectWorkshopController || !m_uiManager)
        return;

    auto context = m_projectSession->snapshot();
    const auto project = m_projectManager ? m_projectManager->currentProject() : nullptr;
    const bool projectDirty = project && project->isModified();
    if (context.projectDirty != projectDirty) {
        const auto transition = m_projectSession->dispatch(
            workshop::WorkshopCommand{workshop::SetProjectDirty{projectDirty}, context.generation});
        if (transition.accepted())
            context = transition.context;
    }

    auto& display = *m_projectDisplayFacts;
    display.projectLabel.reset();
    if (project && context.activeProject && project->id() == context.activeProject->value) {
        display.projectLabel = project->name();
    }

    if (!m_projectDisplayGeneration || *m_projectDisplayGeneration != context.generation.value) {
        display.itemLabel.reset();
        display.previewLabel.reset();
        if (context.activeProjectItem) {
            const auto item = m_projectManager->findOpenItem(context.activeProjectItem->item.value);
            if (item && item->projectId == context.activeProjectItem->project.value)
                display.itemLabel = item->displayName;
        }
        if (context.libraryPreview) {
            const i64 itemId = context.libraryPreview->item.value;
            if (context.libraryPreview->kind == workshop::LibraryItemKind::Model) {
                if (m_libraryManager) {
                    const auto model = m_libraryManager->getModel(itemId);
                    if (model)
                        display.previewLabel = model->name;
                }
            } else if (m_gcodeRepo) {
                const auto gcode = m_gcodeRepo->findById(itemId);
                if (gcode)
                    display.previewLabel = gcode->name;
            }
        }
        m_projectDisplayGeneration = context.generation.value;
    }

    workshop::MachineStatusSnapshot machine;
    machine.label = m_cncController && m_cncController->isSimulating() ? "Virtual CNC" : "CNC";
    machine.connected = m_cncController && m_cncController->isConnected();
    machine.running = m_cncController && m_cncController->isStreaming();
    m_uiManager->setProjectShellSnapshot(m_projectWorkshopController->snapshot(display, machine));
}

void Application::wireProjectPanel() {
    auto* panel = m_uiManager->projectPanel();
    if (!panel)
        return;

    auto activateRef = [this](workshop::ProjectItemRef ref) {
        if (!m_projectManager || !ref.valid()) return false;
        const auto item = m_projectManager->findOpenItem(ref.item.value);
        if (!item || item->projectId != ref.project.value) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Project Item Unavailable",
                "The project changed. Refresh the Project Plan and try again.");
            return false;
        }
        if (item->itemType == ProjectOpenItemType::Operation)
            return beginPrepareCarve(ref);
        return activateProjectOpenItem(*item) != ProjectItemActivationStatus::Rejected;
    };

    panel->setProjectPlanProvider([this]() -> std::optional<ProjectPanelSnapshot> {
        if (!m_projectManager || !m_projectSession) return std::nullopt;
        const auto project = m_projectManager->currentProject();
        const auto context = m_projectSession->snapshot();
        if (!project || !context.activeProject ||
            context.activeProject->value != project->id()) {
            return std::nullopt;
        }

        ProjectPanelSnapshot snapshot;
        snapshot.projectId = project->id();
        snapshot.projectName = project->name();
        snapshot.description = project->description();
        snapshot.notes = project->record().notes;
        snapshot.modified = project->isModified();
        snapshot.hasModels = !project->modelIds().empty();
        if (context.activeProjectItem &&
            context.activeProjectItem->project == *context.activeProject) {
            snapshot.activeItem = context.activeProjectItem->item;
        }

        const auto items = m_projectManager->currentOpenItems();
        auto input = makeProjectPlanInput(*context.activeProject,
                                          project->name(),
                                          items,
                                          context.activeProjectItem);
        std::optional<ProjectPlanRunSourceSnapshot> protectedRun;
        if (m_directCarveRunEffectAdapter) {
            const auto& run = m_directCarveRunEffectAdapter->snapshot();
            const bool running = run.state == DirectCarveRunControlState::Streaming;
            const bool paused = run.state == DirectCarveRunControlState::Paused;
            if (run.identity && run.jobId && (running || paused)) {
                protectedRun = ProjectPlanRunSourceSnapshot{
                    *run.jobId,
                    paused ? project_plan::RunState::Paused
                           : project_plan::RunState::Running,
                    run.identity->setup().toolpath().operationItem(),
                };
            }
        }
        std::optional<ProjectPlanRunSourceSnapshot> advancedRun;
        if (const auto* gcodePanel = m_uiManager->gcodePanel()) {
            if (const auto run = gcodePanel->projectPlanRunSnapshot()) {
                advancedRun = ProjectPlanRunSourceSnapshot{
                    run->jobSourceId,
                    run->state == GCodePanelRunState::Paused
                        ? project_plan::RunState::Paused
                        : project_plan::RunState::Running,
                    std::nullopt,
                };
            }
        }
        if (m_projectPlanRunTruthAdapter) {
            const auto runTruth = m_projectPlanRunTruthAdapter->resolve(
                {*context.activeProject, &items, protectedRun, advancedRun});
            if (runTruth.resolved()) input.liveRun = runTruth.snapshot;
        }
        snapshot.plan = project_plan::ProjectPlanBuilder().build(input);
        return snapshot;
    });
    panel->setOnProjectItemActivated([activateRef](workshop::ProjectItemRef ref) {
        (void)activateRef(ref);
    });
    panel->setOnProjectPlanAction([this, activateRef](const project_plan::NextAction& action) {
        using project_plan::NextActionKind;
        if (action.kind == NextActionKind::None) return;
        if (action.kind == NextActionKind::AddDesignFromLibrary) {
            (void)showDesignLibrary(design_library::LibraryPickerPurpose::AddToProject);
            return;
        }
        if (action.kind == NextActionKind::RepairItem) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Project Item Needs Repair",
                "Refresh, re-import, or replace the changed source before continuing.");
            return;
        }
        const bool prepareAction =
            action.kind == NextActionKind::OpenDesignAndSize ||
            action.kind == NextActionKind::OpenMaterialAndBlank ||
            action.kind == NextActionKind::OpenToolSelection ||
            action.kind == NextActionKind::OpenCarvePreview ||
            action.kind == NextActionKind::OpenMachineSetup ||
            action.kind == NextActionKind::OpenReviewAndRun;
        if (prepareAction) {
            if (action.target) (void)beginPrepareCarve(*action.target);
            return;
        }
        const bool runAction = action.kind == NextActionKind::MonitorRun ||
                               action.kind == NextActionKind::ReconcileRunState ||
                               action.kind == NextActionKind::ReviewInterruptedRun;
        if (runAction) {
            const auto project = m_projectManager
                                     ? m_projectManager->currentProject()
                                     : nullptr;
            if (!action.target || !project || !m_uiManager) return;
            const auto items = m_projectManager->currentOpenItems();
            const auto route = resolveProjectPlanRunActionRoute(
                workshop::ProjectId(project->id()), items, *action.target);
            if (!route.ready()) {
                ToastManager::instance().show(
                    ToastType::Warning, "Run Details Unavailable",
                    "The saved job hierarchy changed. Refresh the Project Plan.");
                return;
            }

            if (action.kind == NextActionKind::MonitorRun) {
                bool exactLiveRun = false;
                if (m_directCarveRunEffectAdapter && route.operation) {
                    const auto& direct = m_directCarveRunEffectAdapter->snapshot();
                    const bool live = direct.state == DirectCarveRunControlState::Streaming ||
                                      direct.state == DirectCarveRunControlState::Paused;
                    exactLiveRun = live && direct.jobId && direct.identity &&
                                   *direct.jobId == route.jobSourceId &&
                                   direct.identity->setup().toolpath().operationItem() ==
                                       *route.operation;
                    if (exactLiveRun) m_uiManager->openWindow("direct_carve");
                }
                if (!exactLiveRun) {
                    const auto* gcode = m_uiManager->gcodePanel();
                    const auto advanced = gcode
                                              ? gcode->projectPlanRunSnapshot()
                                              : std::nullopt;
                    exactLiveRun = advanced &&
                                   advanced->jobSourceId == route.jobSourceId;
                    if (exactLiveRun) m_uiManager->openWindow("gcode_viewer");
                }
                if (!exactLiveRun) {
                    ToastManager::instance().show(
                        ToastType::Warning, "Run State Changed",
                        "The machine run changed. Refresh the Project Plan.");
                }
                return;
            }

            if (m_cncController && m_cncController->isStreaming()) {
                m_uiManager->openWindow(
                    route.surface == ProjectPlanRunActionSurface::DirectCarve
                        ? "direct_carve"
                        : "gcode_viewer");
                return;
            }
            if (route.surface == ProjectPlanRunActionSurface::DirectCarve &&
                route.operation) {
                (void)beginPrepareCarve(*route.operation);
            } else {
                (void)activateRef(route.program);
            }
            return;
        }
        if (action.target) (void)activateRef(*action.target);
    });
    panel->setOpenHomeCallback([this]() { showHome(); });
    panel->setCloseProjectCallback([this]() {
        requestProjectClose(workshop::ProjectClosePurpose::ExplicitClose);
    });
    panel->setSaveProjectCallback([this]() { return m_fileIOManager->saveProject(); });
    panel->setExportProjectCallback([this]() { m_fileIOManager->exportProjectArchive(); });
    panel->setUpdateNotesCallback([this](i64 projectId, std::string notes) {
        if (!m_projectManager) return;
        const auto project = m_projectManager->currentProject();
        if (!project || project->id() != projectId) return;
        project->record().notes = std::move(notes);
        project->markModified();
    });
}

} // namespace dw
