// Design Library lifecycle composition. This unit owns entry, import handoff,
// and acknowledged return; picker policy and durable mutations stay in their
// dedicated modules.

#include "app/application.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app/library_workflow_coordinator.h"
#include "app/library_workflow_adapter.h"
#include "core/cnc/cnc_controller.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/graph/graph_manager.h"
#include "core/library/library_manager.h"
#include "core/project/project.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_flow.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_workshop_controller.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/start_page.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

using design_library::LibraryPickerPurpose;
using library_workflow_adapter::flowAccepted;
using library_workflow_adapter::pickerFailureMessage;

} // namespace

void Application::initializeLibraryWorkflow() {
    m_libraryManager = std::make_unique<LibraryManager>(*m_database);
    m_libraryManager->setGraphManager(m_graphManager.get());
    m_projectManager = std::make_unique<ProjectManager>(*m_database);
    m_modelRepo = std::make_unique<ModelRepository>(*m_database);
    m_gcodeRepo = std::make_unique<GCodeRepository>(*m_database);
    m_libraryWorkflow = std::make_unique<LibraryWorkflowCoordinator>(
        *m_database, *m_libraryManager, *m_projectManager, *m_gcodeRepo);
}

bool Application::showDesignLibrary() {
    return showDesignLibrary(LibraryPickerPurpose::ManageLibrary);
}

workshop::ExperienceMode Application::libraryExperienceMode() const {
    return m_projectWorkshopController && m_projectWorkshopController->guidedEnabled()
               ? workshop::ExperienceMode::Guided
               : workshop::ExperienceMode::Advanced;
}

bool Application::showDesignLibrary(LibraryPickerPurpose purpose) {
    if (!m_libraryWorkflow || !m_projectSession || !m_projectWorkshopController ||
        !m_uiManager) {
        return false;
    }

    const bool machineActionActive =
        m_cncController && m_cncController->isStreaming();
    if (machineActionActive) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Run CNC Is Active",
            "Pause or stop the active machine action before opening the Design Library.");
        return false;
    }

    const auto experience = libraryExperienceMode();
    const bool guided = experience == workshop::ExperienceMode::Guided;
    auto& picker = m_libraryWorkflow->picker();
    const auto existing = picker.snapshot();
    if (existing.active) {
        const auto context = m_projectSession->snapshot();
        if (existing.purpose != purpose ||
            context.route != workshop::WorkshopRoute::DesignLibrary) {
            ToastManager::instance().show(ToastType::Warning,
                                          "Library Task Already Open",
                                          "Finish or cancel the current Library task first.");
            return false;
        }
        m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
        m_uiManager->showStartPage() = false;
        m_uiManager->showProject() = false;
        m_uiManager->showViewport() = true;
        m_uiManager->showProperties() = !guided;
        m_uiManager->openWindow("library");
        if (auto* panel = m_uiManager->libraryPanel()) {
            panel->refresh();
            panel->setPickerState(existing);
        }
        return true;
    }

    auto context = m_projectSession->snapshot();
    const auto navigation = m_projectWorkshopController->dispatch(
        workshop::NavigateWorkshopIntent{
            experience, context.generation, workshop::WorkshopRoute::DesignLibrary});
    if (!navigation.accepted()) {
        ToastManager::instance().show(ToastType::Warning,
                                      "Design Library Blocked",
                                      "Finish the current project or machine decision first.");
        return false;
    }
    context = m_projectSession->snapshot();

    std::string projectName;
    std::vector<workshop::LibraryItemRef> membership;
    if (context.activeProject) {
        const auto project = m_projectManager ? m_projectManager->currentProject() : nullptr;
        if (project && project->id() == context.activeProject->value)
            projectName = project->name();
        membership = m_libraryWorkflow->durableMembership(*context.activeProject);
    }
    const auto begun = picker.dispatch(
        design_library::BeginLibraryPicker{purpose, projectName, std::move(membership)},
        context);
    if (!flowAccepted(begun)) {
        const auto current = m_projectSession->snapshot();
        (void)m_projectWorkshopController->dispatch(workshop::ReturnFromLibraryIntent{
            experience, current.generation});
        ToastManager::instance().show(ToastType::Warning,
                                      "Design Library Blocked",
                                      pickerFailureMessage(begun.reason));
        return false;
    }

    m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
    m_uiManager->showStartPage() = false;
    m_uiManager->showProject() = false;
    m_uiManager->showViewport() = true;
    m_uiManager->showProperties() = !guided;
    m_uiManager->openWindow("library");
    if (auto* panel = m_uiManager->libraryPanel()) {
        panel->refresh();
        panel->setPickerState(begun.snapshot);
    }
    return true;
}

bool Application::requestLibraryReturn(std::string& errorMessage) {
    errorMessage.clear();
    if (!m_libraryWorkflow || !m_projectSession || !m_projectWorkshopController ||
        !m_uiManager) {
        errorMessage = "The Design Library return path is unavailable.";
        return false;
    }

    auto& picker = m_libraryWorkflow->picker();
    std::optional<workshop::ProjectItemRef> returnItem;
    if (picker.snapshot().active) {
        const auto canceled = picker.dispatch(design_library::CancelLibraryPicker{},
                                              m_projectSession->snapshot());
        if (!flowAccepted(canceled)) {
            errorMessage = pickerFailureMessage(canceled.reason);
            if (auto* panel = m_uiManager->libraryPanel())
                panel->setPickerState(canceled.snapshot, errorMessage);
            return false;
        }

        if (canceled.request) {
            const auto* request =
                std::get_if<design_library::RestoreLibraryContextRequest>(&*canceled.request);
            if (!request) {
                errorMessage = "The Design Library produced an invalid return request.";
                return false;
            }
            returnItem = request->projectItem;
            const auto restored = m_projectWorkshopController->dispatch(
                workshop::ReturnFromLibraryIntent{
                    libraryExperienceMode(), request->expectedGeneration});
            const bool applied = restored.status == workshop::TransitionStatus::Applied;
            const auto completed = picker.dispatch(
                design_library::CompleteLibraryRestore{request->token, applied},
                m_projectSession->snapshot());
            if (!applied || completed.snapshot.active) {
                errorMessage = applied
                                   ? "The workspace changed while the Library was returning."
                                   : "The previous workspace could not be restored.";
                if (auto* panel = m_uiManager->libraryPanel())
                    panel->setPickerState(completed.snapshot, errorMessage);
                return false;
            }
        }
    }

    auto context = m_projectSession->snapshot();
    if (context.route == workshop::WorkshopRoute::DesignLibrary) {
        const auto recovered = m_projectWorkshopController->dispatch(
            workshop::ReturnFromLibraryIntent{
                libraryExperienceMode(), context.generation});
        if (!recovered.accepted()) {
            errorMessage = "The previous workspace could not be restored.";
            return false;
        }
        context = m_projectSession->snapshot();
    }
    if (!returnItem && context.activeProjectItem)
        returnItem = context.activeProjectItem;

    m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
    m_uiManager->showLibrary() = false;
    if (auto* panel = m_uiManager->libraryPanel())
        panel->clearPickerState();

    if (context.route == workshop::WorkshopRoute::Project && context.activeProject) {
        m_uiManager->showStartPage() = false;
        m_uiManager->showProject() = true;
        m_uiManager->showViewport() = true;
        m_uiManager->showProperties() = true;
        m_uiManager->openWindow("project");
        if (returnItem && returnItem->project == *context.activeProject && m_projectManager) {
            const auto item = m_projectManager->findOpenItem(returnItem->item.value);
            if (item && item->projectId == returnItem->project.value)
                (void)activateProjectOpenItem(*item, false);
        } else if (!context.activeProjectItem) {
            invalidateProjectFocus();
        }
    } else {
        invalidateProjectFocus();
        m_uiManager->showProject() = false;
        m_uiManager->showViewport() = false;
        m_uiManager->showProperties() = false;
        m_uiManager->openWindow("start_page");
    }
    return true;
}

void Application::handleCompletedLibraryImports(
    const std::vector<ImportedLibraryItem>& items) {
    if (!m_libraryWorkflow || !m_projectSession || items.empty())
        return;

    const auto active = m_libraryWorkflow->picker().snapshot();
    const auto purpose = active.active
                             ? active.purpose
                             : m_pendingImportLibraryPurpose.value_or(
                                   LibraryPickerPurpose::ManageLibrary);
    m_pendingImportLibraryPurpose.reset();
    if (!showDesignLibrary(purpose))
        return;

    std::vector<workshop::LibraryItemRef> imported;
    imported.reserve(items.size());
    for (const auto& item : items) {
        if (!item.valid())
            continue;
        switch (item.kind) {
        case ImportedLibraryItemKind::Model:
            imported.push_back({workshop::LibraryItemKind::Model,
                                workshop::LibraryItemId(item.id)});
            break;
        case ImportedLibraryItemKind::GCode:
            imported.push_back({workshop::LibraryItemKind::GCode,
                                workshop::LibraryItemId(item.id)});
            break;
        }
    }
    if (imported.empty())
        return;

    const auto offered = m_libraryWorkflow->picker().dispatch(
        design_library::OfferImportedLibraryItems{std::move(imported)},
        m_projectSession->snapshot());
    if (auto* panel = m_uiManager->libraryPanel()) {
        panel->refresh();
        panel->setPickerState(
            offered.snapshot,
            flowAccepted(offered)
                ? std::string{}
                : pickerFailureMessage(offered.reason));
    }
}

} // namespace dw
