// Application composition for the project-centered workshop shell. This file
// owns UI intent wiring only; ProjectSession remains the policy authority.

#include "app/application.h"
#include "app/library_workflow_coordinator.h"
#include "app/project_contents_adapter.h"

#include <cstdint>
#include <string>
#include <utility>

#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "core/database/gcode_repository.h"
#include "core/library/library_manager.h"
#include "core/materials/material_manager.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_item_load_guard.h"
#include "modules/workshop/project_resume.h"
#include "modules/workshop/project_workshop_controller.h"
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

    panel->setProjectSnapshotProvider([this]() -> std::optional<ProjectPanelSnapshot> {
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
        snapshot.contents =
            makeProjectContents(*context.activeProject, m_projectManager->currentOpenItems());
        if (context.activeProjectItem &&
            context.activeProjectItem->project == *context.activeProject) {
            snapshot.activeItem = context.activeProjectItem->item;
        }
        return snapshot;
    });
    panel->setOnProjectItemActivated([this](workshop::ProjectItemRef ref) {
        if (!m_projectManager || !ref.valid()) return;
        const auto item = m_projectManager->findOpenItem(ref.item.value);
        if (!item || item->projectId != ref.project.value) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Project Item Unavailable",
                "The project changed. Try opening the item again.");
            return;
        }
        if (item->itemType == ProjectOpenItemType::Operation) {
            (void)beginPrepareCarve(ref);
            return;
        }
        (void)activateProjectOpenItem(*item);
    });
    panel->setOnOpenInCam([this](workshop::ProjectItemRef design) {
        (void)beginPrepareCarve(design);
    });
    panel->setAddFromLibraryCallback([this]() {
        (void)showDesignLibrary(design_library::LibraryPickerPurpose::AddToProject);
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
