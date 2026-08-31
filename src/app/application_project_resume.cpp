// Restart-resume composition and the shared project-item activation adapter.
// The render-independent coordinator owns decisions; this file supplies the
// database, ProjectSession, UI, and file-backed persistence ports.

#include "app/application.h"
#include "app/carve_preparation_adapter.h"
#include "app/library_workflow_coordinator.h"
#include "app/project_session_integration.h"

#include <filesystem>
#include <utility>

#include "app/project_resume_file_store.h"
#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "core/database/cost_repository.h"
#include "core/database/cut_plan_repository.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/materials/material_manager.h"
#include "core/optimizer/cut_list_file.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "managers/ui_manager.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_item_load_guard.h"
#include "modules/workshop/project_resume.h"
#include "modules/workshop/project_workshop_controller.h"
#include "ui/panels/cost_panel.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/start_page.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

Path normalizedProjectPath(const Path& path) {
    std::error_code error;
    const Path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

workshop::ResumeProjectStatus resumeStatus(ProjectStorageValidationStatus status) {
    switch (status) {
    case ProjectStorageValidationStatus::Ready:
        return workshop::ResumeProjectStatus::Ready;
    case ProjectStorageValidationStatus::MissingRecord:
        return workshop::ResumeProjectStatus::Missing;
    case ProjectStorageValidationStatus::IdentityMismatch:
        return workshop::ResumeProjectStatus::IdentityMismatch;
    case ProjectStorageValidationStatus::MissingPath:
    case ProjectStorageValidationStatus::InvalidDirectory:
    case ProjectStorageValidationStatus::DuplicateClaim:
    case ProjectStorageValidationStatus::InvalidTemporaryOwnership:
        return workshop::ResumeProjectStatus::InvalidStorage;
    }
    return workshop::ResumeProjectStatus::InvalidStorage;
}

const char* itemSelectionFailure(workshop::TransitionReason reason) {
    switch (reason) {
    case workshop::TransitionReason::ActiveRun:
        return "Finish or stop the active run before changing project items.";
    case workshop::TransitionReason::UnsavedPreparation:
        return "Finish or discard the current preparation before changing items.";
    case workshop::TransitionReason::PendingConfirmation:
        return "Finish the open project decision before changing items.";
    case workshop::TransitionReason::StaleGeneration:
        return "The project context changed. Select the item again.";
    default:
        return "This item no longer belongs to the active project.";
    }
}

} // namespace

void Application::initializeProjectResume() {
    m_projectResumeStore = std::make_unique<ProjectResumeFileStore>();
    workshop::ProjectResumeCallbacks callbacks;
    callbacks.inspectProject = [this](const workshop::ProjectResumeBookmark& bookmark) {
        if (!m_projectManager)
            return workshop::ResumeProjectStatus::Missing;
        return resumeStatus(m_projectManager->validateProjectStorage(bookmark.project.value));
    };
    callbacks.activateProject = [this](workshop::ProjectId projectId) {
        const auto record = m_projectManager ? m_projectManager->getProjectInfo(projectId.value)
                                             : std::nullopt;
        if (!record)
            return workshop::ResumeActivationStatus::Rejected;
        const uint64_t expectedGeneration = projectSessionGeneration();
        auto project = m_projectManager ? m_projectManager->open(projectId.value) : nullptr;
        if (!project)
            return workshop::ResumeActivationStatus::Rejected;

        requestProjectActivation(std::move(project), expectedGeneration);
        const auto context = m_projectSession->snapshot();
        if (context.activeProject && *context.activeProject == projectId) {
            const auto current = m_projectManager->currentProject();
            const auto directory = m_projectManager->currentDirectory();
            const bool storageMatches =
                m_projectManager->validateProjectStorage(projectId.value) ==
                    ProjectStorageValidationStatus::Ready &&
                current && current->id() == projectId.value && directory &&
                // record->filePath may be a durable network URL (smb://...)
                // while root() is the mounted local path; resolve before
                // comparing or network projects never match.
                normalizedProjectPath(directory->root()) ==
                    normalizedProjectPath(PathResolver::resolve(
                        record->filePath, PathCategory::Projects));
            if (!storageMatches) {
                (void)m_projectSessionIntegration->closeProject();
                return workshop::ResumeActivationStatus::Rejected;
            }
            m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
            m_uiManager->showStartPage() = false;
            m_uiManager->openWindow("project");
            return workshop::ResumeActivationStatus::Applied;
        }
        if (context.generation.value != expectedGeneration)
            return workshop::ResumeActivationStatus::Superseded;
        return workshop::ResumeActivationStatus::Rejected;
    };
    callbacks.inspectItem = [this](workshop::ProjectItemRef itemRef) {
        if (!m_projectManager || !m_projectManager->currentProject() ||
            m_projectManager->currentProject()->id() != itemRef.project.value) {
            return workshop::ResumeItemStatus::ForeignProject;
        }
        (void)m_projectManager->currentOpenItems();
        const auto item = m_projectManager->findOpenItem(itemRef.item.value);
        if (!item)
            return workshop::ResumeItemStatus::Missing;
        if (item->projectId != itemRef.project.value)
            return workshop::ResumeItemStatus::ForeignProject;
        if (item->status == ProjectOpenItemStatus::Missing)
            return workshop::ResumeItemStatus::Missing;
        if (item->status == ProjectOpenItemStatus::Stale)
            return workshop::ResumeItemStatus::Stale;
        return workshop::ResumeItemStatus::Ready;
    };
    callbacks.activateItem = [this](workshop::ProjectItemRef itemRef) {
        const auto item = m_projectManager ? m_projectManager->findOpenItem(itemRef.item.value)
                                           : std::nullopt;
        if (!item || item->projectId != itemRef.project.value)
            return workshop::ResumeActivationStatus::Rejected;
        switch (activateProjectOpenItem(*item, false)) {
        case ProjectItemActivationStatus::Applied:
            return workshop::ResumeActivationStatus::Applied;
        case ProjectItemActivationStatus::Pending:
            return workshop::ResumeActivationStatus::Pending;
        case ProjectItemActivationStatus::Rejected:
            return workshop::ResumeActivationStatus::Rejected;
        }
        return workshop::ResumeActivationStatus::Rejected;
    };
    callbacks.showHome = [this]() { showHome(); };
    m_projectResumeCoordinator = std::make_unique<workshop::ProjectResumeCoordinator>(
        *m_projectResumeStore, std::move(callbacks));
}

bool Application::restoreProjectResume() {
    if (!m_projectResumeCoordinator) {
        showHome();
        return false;
    }
    const auto result = m_projectResumeCoordinator->restore();
    if (!result.persistenceHealthy) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Resume State Needs Attention",
            "Digital Workshop could not repair its automatic-resume bookmark. Your project data was not changed.");
    }
    return result.projectRestored;
}

void Application::showHome(bool beginNamedProject) {
    if (!m_uiManager)
        return;
    if (m_libraryWorkflow && m_libraryWorkflow->picker().snapshot().active) {
        std::string error;
        if (!requestLibraryReturn(error)) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Library Navigation Blocked",
                error.empty() ? "Finish the current Library action before opening Home."
                              : error);
            return;
        }
    }
    const bool machineActionActive =
        m_cncController && m_cncController->isStreaming();
    if (machineActionActive) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Run CNC Is Active",
            "Finish or stop the active run before leaving the machine workspace.");
        return;
    }
    if (m_projectSession && m_projectWorkshopController) {
        const auto context = m_projectSession->snapshot();
        const auto transition = m_projectWorkshopController->dispatch(
            workshop::NavigateWorkshopIntent{workshop::ExperienceMode::Advanced,
                                               context.generation,
                                               workshop::WorkshopRoute::Home});
        if (!transition.accepted()) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Home Navigation Blocked",
                "Finish the current project or machine decision before opening Home.");
            return;
        }
    }
    m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
    m_uiManager->showLibrary() = false;
    m_uiManager->showProject() = false;
    m_uiManager->showProperties() = false;
    m_uiManager->showViewport() = false;
    m_uiManager->openWindow("start_page");
    if (m_uiManager->startPage()) {
        m_uiManager->startPage()->requestFocus();
        if (beginNamedProject)
            m_uiManager->startPage()->beginNamedProject();
    }
}

Application::ProjectItemActivationStatus Application::activateProjectOpenItem(
    const ProjectOpenItem& item, bool notifyFailure) {
    if (!m_projectSession || !m_projectWorkshopController || item.id <= 0 || item.projectId <= 0)
        return ProjectItemActivationStatus::Rejected;
    if (item.status == ProjectOpenItemStatus::Missing ||
        item.status == ProjectOpenItemStatus::Stale) {
        if (notifyFailure) {
            ToastManager::instance().show(ToastType::Warning,
                                          "Project Item Unavailable",
                                          "Refresh or repair this project item before opening it.");
        }
        return ProjectItemActivationStatus::Rejected;
    }

    const auto context = m_projectSession->snapshot();
    const workshop::ProjectItemRef itemRef{workshop::ProjectId(item.projectId),
                                           workshop::ProjectItemId(item.id)};
    const auto transition =
        m_projectWorkshopController->dispatch(workshop::SelectProjectItemIntent{
            workshop::ExperienceMode::Advanced, context.generation, itemRef});
    if (!transition.accepted()) {
        if (transition.status == workshop::TransitionStatus::ConfirmationRequired &&
            transition.confirmation) {
            (void)m_projectSession->dispatch(workshop::WorkshopCommand{
                workshop::ResolvePendingTransition{*transition.confirmation,
                                                   workshop::PendingTransitionResolution::Cancel},
                std::nullopt});
        }
        if (notifyFailure) {
            ToastManager::instance().show(ToastType::Warning,
                                          "Project Item Locked",
                                          itemSelectionFailure(transition.reason));
        }
        return ProjectItemActivationStatus::Rejected;
    }

    const auto asyncCompletion = [this, itemRef](bool opened) {
        // Model loading already supplies the concrete failure toast. This
        // callback only commits or retracts project-item identity.
        (void)completeProjectItemActivation(itemRef, opened, false);
    };
    const auto contentStatus = openProjectItemContent(item, asyncCompletion);
    if (contentStatus == ProjectItemContentStatus::Unavailable) {
        (void)completeProjectItemActivation(itemRef, false, notifyFailure);
        return ProjectItemActivationStatus::Rejected;
    }
    if (contentStatus == ProjectItemContentStatus::Pending) {
        m_projectDisplayGeneration.reset();
        return ProjectItemActivationStatus::Pending;
    }

    return completeProjectItemActivation(itemRef, true, notifyFailure)
               ? ProjectItemActivationStatus::Applied
               : ProjectItemActivationStatus::Rejected;
}

Application::ProjectItemContentStatus Application::openProjectItemContent(
    const ProjectOpenItem& item, std::function<void(bool)> completion) {
    const i64 sourceId = item.sourceId.value_or(-1);
    const workshop::ProjectItemRef itemRef{workshop::ProjectId(item.projectId),
                                           workshop::ProjectItemId(item.id)};
    const auto resultStillCurrent = [this, itemRef]() {
        if (!m_projectSession)
            return false;
        return workshop::projectItemLoadCanCommit(m_projectSession->snapshot(), itemRef);
    };
    switch (item.itemType) {
    case ProjectOpenItemType::Model:
        if (sourceId <= 0 || !m_modelRepo || !m_modelRepo->findById(sourceId))
            return ProjectItemContentStatus::Unavailable;
        onModelSelected(sourceId, [completion = std::move(completion)](
                                      ModelSelectionStatus status) mutable {
            if (status == ModelSelectionStatus::Superseded)
                return;
            const bool loaded = status == ModelSelectionStatus::Loaded;
            if (completion)
                completion(loaded);
        }, resultStillCurrent);
        return ProjectItemContentStatus::Pending;
    case ProjectOpenItemType::Gcode: {
        if (sourceId <= 0)
            return ProjectItemContentStatus::Unavailable;
        const auto record = m_gcodeRepo ? m_gcodeRepo->findById(sourceId) : std::nullopt;
        auto* panel = m_uiManager->gcodePanel();
        if (!record || !panel)
            return ProjectItemContentStatus::Unavailable;
        if (!panel->loadFile(
                PathResolver::resolve(record->filePath, PathCategory::GCode).string())) {
            panel->clear();
            return ProjectItemContentStatus::Unavailable;
        }
        // Project G-code opens as geometry first. The sender remains an
        // explicit action so inspecting a file cannot accidentally imply that
        // it is ready to run.
        m_uiManager->openWindow("viewport");
        return ProjectItemContentStatus::Opened;
    }
    case ProjectOpenItemType::Operation: {
        if (!m_projectManager)
            return ProjectItemContentStatus::Unavailable;
        const auto context = m_projectSession->snapshot();
        const auto revision =
            carve_preparation::PreparationRevision{context.generation.value};
        const auto token =
            carve_preparation::PreparationToken{m_nextPreparationToken++};
        // The pin resolve validates the operation's identity chain before
        // falling back to the parent model.
        const auto pinResult = resolvePrepareCarvePin(
            context.activeProject,
            itemRef,
            token,
            revision,
            m_projectManager->currentOpenItems());
        if (pinResult.status != PrepareCarveAdapterStatus::Ready || !pinResult.pin)
            return ProjectItemContentStatus::Unavailable;
        const i64 parentModelId = pinResult.pin->modelSource().item.value;
        const auto modelRecord =
            m_modelRepo ? m_modelRepo->findById(parentModelId) : std::nullopt;
        if (!modelRecord)
            return ProjectItemContentStatus::Unavailable;
        // The saved operation resolves to its parent model, opens the CAM
        // window, and becomes the CAM panel's active setup.
        CamActiveSetup setup;
        setup.projectId = item.projectId;
        setup.operationItemId = item.id;
        setup.modelId = parentModelId;
        setup.modelName = modelRecord->name;
        setup.meshPath =
            PathResolver::resolve(modelRecord->filePath, PathCategory::Models).string();
        onModelSelected(parentModelId,
                        [this, setup = std::move(setup),
                         completion = std::move(completion)](
                            ModelSelectionStatus status) mutable {
                            if (status == ModelSelectionStatus::Superseded)
                                return;
                            const bool opened = status == ModelSelectionStatus::Loaded;
                            if (opened && m_uiManager) {
                                m_camActiveSetup = std::move(setup);
                                m_uiManager->openWindow("direct_carve");
                            }
                            if (completion)
                                completion(opened);
                        },
                        resultStillCurrent);
        return ProjectItemContentStatus::Pending;
    }
    case ProjectOpenItemType::Material:
        if (sourceId <= 0 || !m_materialManager || !m_materialManager->getMaterial(sourceId))
            return ProjectItemContentStatus::Unavailable;
        if (auto* materials = m_uiManager->materialsPanel()) {
            m_uiManager->openWindow("materials");
            materials->selectMaterial(sourceId);
            return ProjectItemContentStatus::Opened;
        }
        return ProjectItemContentStatus::Unavailable;
    case ProjectOpenItemType::Cost:
        if (sourceId <= 0 || !m_costRepo || !m_costRepo->findById(sourceId))
            return ProjectItemContentStatus::Unavailable;
        if (auto* costs = m_uiManager->costPanel()) {
            m_uiManager->openWindow("project_costing");
            costs->selectRecord(sourceId);
            return ProjectItemContentStatus::Opened;
        }
        return ProjectItemContentStatus::Unavailable;
    case ProjectOpenItemType::CutPlan: {
        if (sourceId <= 0)
            return ProjectItemContentStatus::Unavailable;
        const auto record = m_cutPlanRepo ? m_cutPlanRepo->findById(sourceId) : std::nullopt;
        if (!record || !m_cutListFile || !m_uiManager->cutOptimizerPanel())
            return ProjectItemContentStatus::Unavailable;
        CutListFile::LoadResult result;
        result.name = record->name;
        result.algorithm = record->algorithm;
        result.allowRotation = record->allowRotation;
        result.kerf = record->kerf;
        result.margin = record->margin;
        if (!record->sheetConfigJson.empty())
            result.sheet = CutPlanRepository::jsonToSheet(record->sheetConfigJson);
        if (!record->partsJson.empty())
            result.parts = CutPlanRepository::jsonToParts(record->partsJson);
        if (!record->resultJson.empty())
            result.result = CutPlanRepository::jsonToCutPlan(record->resultJson);
        m_uiManager->openWindow("cut_optimizer");
        m_uiManager->cutOptimizerPanel()->loadCutPlan(result);
        return ProjectItemContentStatus::Opened;
    }
    case ProjectOpenItemType::Stock:
    case ProjectOpenItemType::Tool:
    case ProjectOpenItemType::Job:
    case ProjectOpenItemType::Labor:
    case ProjectOpenItemType::Consumable:
    case ProjectOpenItemType::Zeroing:
        // These item types currently have no dedicated detail surface. Restoring
        // their project identity is still a truthful, useful resume result.
        return ProjectItemContentStatus::IdentityOnly;
    }
    return ProjectItemContentStatus::Unavailable;
}

bool Application::completeProjectItemActivation(const workshop::ProjectItemRef& itemRef,
                                                bool opened,
                                                bool notifyFailure) {
    if (!m_projectSession || !m_projectWorkshopController)
        return false;
    const auto context = m_projectSession->snapshot();
    if (!workshop::projectItemLoadCanCommit(context, itemRef)) {
        return false;
    }

    if (!opened) {
        (void)m_projectWorkshopController->dispatch(workshop::ClearProjectItemIntent{
            workshop::ExperienceMode::Advanced, context.generation});
        if (m_uiManager && m_uiManager->projectPanel())
            m_uiManager->projectPanel()->clearSelection();
        clearProjectResumeItem();
        m_projectDisplayGeneration.reset();
        if (m_uiManager && m_uiManager->viewportPanel()) {
            m_uiManager->viewportPanel()->setPresentationIdentity(
                viewport::PresentationIdentity::none());
        }
        if (notifyFailure) {
            ToastManager::instance().show(
                ToastType::Warning,
                "Project Item Unavailable",
                "The item is still listed, but its content could not be opened.");
        }
        return false;
    }

    if (m_projectResumeCoordinator && !m_projectResumeCoordinator->rememberItem(itemRef)) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Resume Not Saved",
            "The item opened, but Digital Workshop could not remember it for the next launch.");
    }
    if (m_uiManager && m_uiManager->viewportPanel() && m_projectManager) {
        const auto item = m_projectManager->findOpenItem(itemRef.item.value);
        const auto project = m_projectManager->currentProject();
        if (item && project && item->projectId == project->id()) {
            const std::string& itemLabel =
                item->displayName.empty() ? item->sourceKey : item->displayName;
            m_uiManager->viewportPanel()->setPresentationIdentity(
                viewport::PresentationIdentity::projectItem(project->name(), itemLabel));
        } else {
            m_uiManager->viewportPanel()->setPresentationIdentity(
                viewport::PresentationIdentity::none());
        }
    }
    m_projectDisplayGeneration.reset();
    return true;
}

void Application::clearProjectResumeItem() {
    if (m_projectResumeCoordinator && !m_projectResumeCoordinator->clearItem()) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Resume State Not Cleared",
            "The unavailable item could not be removed from automatic resume.");
    }
}

} // namespace dw
