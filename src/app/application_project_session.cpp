// Application composition for the authoritative project session. UI and file
// services emit intents here; only ProjectSessionIntegration commits identity.

#include "app/application.h"

#include <utility>

#include "app/project_session_integration.h"
#include "app/workspace.h"
#include "core/cnc/cnc_controller.h"
#include "core/database/cut_plan_repository.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/optimizer/cut_list_file.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_resume.h"
#include "modules/workshop/project_workshop_controller.h"
#include "ui/dialogs/message_dialog.h"
#include "ui/panels/cost_panel.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/direct_carve_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

namespace dw {

void Application::initializeProjectSession() {
    m_projectSession = std::make_unique<workshop::ProjectSession>();
    m_projectWorkshopController =
        std::make_unique<workshop::ProjectWorkshopController>(*m_projectSession, false);
    m_uiManager->setExperienceModeAccessors(
        [this]() {
            return m_projectWorkshopController && m_projectWorkshopController->guidedEnabled();
        },
        [this](bool guided) {
            if (m_projectWorkshopController)
                m_projectWorkshopController->setGuidedEnabled(guided);
        });
    m_projectDisplayFacts = std::make_unique<workshop::ProjectDisplayFacts>();
    initializeProjectResume();
    m_projectSessionIntegration = std::make_unique<ProjectSessionIntegration>(
        *m_projectSession, *m_projectManager, [this](const ProjectSessionCommit& commit) {
            invalidateProjectFocus();
            m_projectDisplayGeneration.reset();
            if (!commit.activeProject || !m_projectResumeCoordinator)
                return;
            const auto project = m_projectManager->currentProject();
            if (!project || project->id() != commit.activeProject->value)
                return;
            if (!m_projectResumeCoordinator->rememberProject(*commit.activeProject)) {
                ToastManager::instance().show(
                    ToastType::Warning,
                    "Resume Not Saved",
                    "The project is open, but Digital Workshop could not remember it for the next launch.");
            }
        },
        ProjectSessionIntegration::SaveCallback{},
        [this]() {
            return m_uiManager && m_uiManager->directCarvePanel() &&
                   m_uiManager->directCarvePanel()->savePreparation();
        });
}

void Application::initializeFileIOManager() {
    m_fileIOManager = std::make_unique<FileIOManager>(m_database.get(),
                                                      m_libraryManager.get(),
                                                      m_projectManager.get(),
                                                      m_importQueue.get(),
                                                      m_workspace.get(),
                                                      m_uiManager->fileDialog(),
                                                      m_thumbnailGenerator.get(),
                                                      m_projectExportManager.get());
    m_fileIOManager->setProgressDialog(m_uiManager->progressDialog());
    m_fileIOManager->setMainThreadQueue(m_mainThreadQueue.get());
    m_fileIOManager->setProjectGenerationCallback([this]() { return projectSessionGeneration(); });
    m_fileIOManager->setProjectSavedCallback([this]() {
        if (m_projectSession) {
            (void)m_projectSession->dispatch(
                workshop::WorkshopCommand{workshop::SetProjectDirty{false}, std::nullopt});
        }
    });
    m_fileIOManager->setProjectActivationCallback(
        [this](std::shared_ptr<Project> project,
               std::optional<uint64_t> expectedGeneration,
               FileIOManager::ProjectActivationCompletion completion) {
            requestProjectActivation(std::move(project), expectedGeneration, std::move(completion));
        });
    m_fileIOManager->setThumbnailCallback(
        [this](int64_t modelId, Mesh& mesh) { return generateMaterialThumbnail(modelId, mesh); });
    m_fileIOManager->setGCodeCallback([this](const std::string& path) {
        m_uiManager->setWorkspaceMode(WorkspaceMode::CNC);
        if (auto* panel = m_uiManager->gcodePanel()) {
            panel->setOpen(true);
            panel->loadFile(path);
        }
    });
}

void Application::requestProjectActivation(std::shared_ptr<Project> project,
                                           std::optional<uint64_t> expectedGeneration,
                                           std::function<void(bool)> completion) {
    if (!m_projectSessionIntegration) {
        if (completion)
            completion(false);
        return;
    }
    const bool machineActionActive =
        (m_cncController && m_cncController->isStreaming()) ||
        (m_uiManager && m_uiManager->directCarvePanel() &&
         m_uiManager->directCarvePanel()->hasActiveMachineAction());
    if (machineActionActive) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Run CNC Is Active",
            "Finish or stop the active run before changing projects.");
        if (completion)
            completion(false);
        return;
    }
    if (m_temporaryProjectDecisionPending) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Decision Required",
            "Finish the temporary project decision before changing projects.");
        if (completion)
            completion(false);
        return;
    }
    const auto expected =
        expectedGeneration.has_value()
            ? std::optional<workshop::ContextGeneration>{workshop::ContextGeneration{
                  *expectedGeneration}}
            : std::nullopt;
    finishProjectTransition(m_projectSessionIntegration->activateProject(std::move(project),
                                                                         expected),
                            std::move(completion));
}

void Application::requestProjectClose(workshop::ProjectClosePurpose purpose,
                                      std::function<void(bool)> completion) {
    if (!m_projectSessionIntegration) {
        if (completion)
            completion(false);
        return;
    }
    const bool machineActionActive =
        (m_cncController && m_cncController->isStreaming()) ||
        (m_uiManager && m_uiManager->directCarvePanel() &&
         m_uiManager->directCarvePanel()->hasActiveMachineAction());
    if (machineActionActive) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Run CNC Is Active",
            "Finish or stop the active run before closing the project or quitting.");
        if (completion)
            completion(false);
        return;
    }
    if (m_temporaryProjectDecisionPending) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Decision Required",
            "Finish the temporary project decision before closing or quitting.");
        if (completion)
            completion(false);
        return;
    }
    if (m_projectSessionIntegration->hasPendingCommit()) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Decision Required",
            "Finish the open project decision before closing or quitting.");
        if (completion)
            completion(false);
        return;
    }
    if (SavePromptDialog::instance().isOpen()) {
        ToastManager::instance().show(ToastType::Warning,
                                      "Decision Required",
                                      "Finish the open save decision before closing or quitting.");
        if (completion)
            completion(false);
        return;
    }

    std::function<void(bool)> completeClose =
        [this, purpose, completion = std::move(completion)](bool closed) mutable {
            if (closed && m_projectResumeCoordinator &&
                !m_projectResumeCoordinator->completeClose(purpose)) {
                ToastManager::instance().show(
                    ToastType::Warning,
                    "Resume State Not Cleared",
                    "The project closed, but its automatic-resume bookmark could not be removed.");
            }
            if (closed && purpose == workshop::ProjectClosePurpose::ExplicitClose)
                showHome();
            if (completion)
                completion(closed);
        };

    auto project = m_projectManager->currentProject();
    if (!project || !project->isTemporary()) {
        finishProjectTransition(m_projectSessionIntegration->closeProject(),
                                std::move(completeClose));
        return;
    }

    const i64 projectId = project->id();
    const Path projectRoot = m_projectManager->currentDirectory()
                                 ? m_projectManager->currentDirectory()->root()
                                 : project->filePath();
    const uint64_t decisionGeneration = projectSessionGeneration();
    m_temporaryProjectDecisionPending = true;
    SavePromptDialog::prompt(
        "Unsaved Project",
        "The project \"" + project->name() +
            "\" is temporary. Save it, discard it, or cancel to stay here.",
        [this,
         project,
         projectId,
         projectRoot,
         decisionGeneration,
         completeClose = std::move(completeClose)](DialogResult choice) mutable {
            m_temporaryProjectDecisionPending = false;
            const auto current = m_projectManager->currentProject();
            if (!current || current->id() != projectId ||
                projectSessionGeneration() != decisionGeneration) {
                ToastManager::instance().show(
                    ToastType::Warning,
                    "Project Changed",
                    "The close decision was stale; no project data was changed.");
                completeClose(false);
                return;
            }
            if (choice == DialogResult::Cancel) {
                completeClose(false);
                return;
            }
            if (choice == DialogResult::Yes) {
                if (!m_projectManager->saveTemporaryProject()) {
                    ToastManager::instance().show(ToastType::Error,
                                                  "Project Save Failed",
                                                  "The temporary project was not closed.");
                    completeClose(false);
                    return;
                }
                finishProjectTransition(m_projectSessionIntegration->closeProject(),
                                        std::move(completeClose));
                return;
            }

            auto closeResult = m_projectSessionIntegration->closeProject();
            if (closeResult.transition.status == workshop::TransitionStatus::ConfirmationRequired &&
                closeResult.transition.confirmation.has_value()) {
                closeResult = m_projectSessionIntegration->resolvePending(
                    *closeResult.transition.confirmation, ProjectTransitionChoice::Discard);
            }
            finishProjectTransition(
                std::move(closeResult),
                [this, project, projectId, projectRoot, completeClose = std::move(completeClose)](
                    bool closed) mutable {
                    if (!closed) {
                        completeClose(false);
                        return;
                    }

                    if (m_projectManager->discardTemporaryProjectData(projectId, projectRoot)) {
                        if (m_projectResumeCoordinator &&
                            !m_projectResumeCoordinator->completeDestruction()) {
                            ToastManager::instance().show(
                                ToastType::Warning,
                                "Resume State Not Cleared",
                                "The discarded project was removed, but its automatic-resume "
                                "bookmark could not be cleared.");
                        }
                        completeClose(true);
                        return;
                    }

                    ToastManager::instance().show(
                        ToastType::Error,
                        "Project Discard Failed",
                        "The temporary project was restored so you can try again safely.");
                    requestProjectActivation(project,
                                             std::nullopt,
                                             [completeClose = std::move(completeClose)](
                                                 bool) mutable { completeClose(false); });
                });
        });
}

void Application::finishProjectTransition(ProjectSessionIntegrationResult result,
                                          std::function<void(bool)> completion) {
    using workshop::TransitionStatus;
    if (result.transition.status == TransitionStatus::ConfirmationRequired &&
        result.transition.confirmation.has_value()) {
        const auto token = *result.transition.confirmation;
        std::string message;
        if (result.error == ProjectSessionIntegrationError::SaveFailed) {
            message = "The project could not be saved. Try again, discard the changes, "
                      "or cancel to stay here.";
        } else if (result.transition.pendingChanges.unsavedPreparation) {
            message = "This project has unfinished carve preparation. Save the project, "
                      "discard the unfinished work, or cancel to stay here.";
        } else {
            message = "This project has unsaved changes. Save before continuing?";
        }

        SavePromptDialog::prompt(
            result.error == ProjectSessionIntegrationError::SaveFailed ? "Project Save Failed"
                                                                       : "Unsaved Project",
            message,
            [this, token, completion = std::move(completion)](DialogResult choice) mutable {
                if (choice == DialogResult::Cancel) {
                    (void)m_projectSessionIntegration->resolvePending(
                        token, ProjectTransitionChoice::Cancel);
                    if (completion)
                        completion(false);
                    return;
                }

                const auto resolution = choice == DialogResult::Yes
                                            ? ProjectTransitionChoice::Save
                                            : ProjectTransitionChoice::Discard;
                finishProjectTransition(m_projectSessionIntegration->resolvePending(token,
                                                                                    resolution),
                                        std::move(completion));
            });
        return;
    }

    const bool accepted = result.error == ProjectSessionIntegrationError::None &&
                          result.transition.accepted();
    if (!accepted && result.transition.reason == workshop::TransitionReason::ActiveRun) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Project Locked",
            "Finish or stop the active machine run before changing projects.");
    }
    if (completion)
        completion(accepted);
}

void Application::invalidateProjectFocus() {
    if (m_focusedModelId > 0 && !m_focusedModelIsLibraryPreview && m_database &&
        m_uiManager && m_uiManager->viewportPanel()) {
        ModelRepository repository(*m_database);
        repository.updateCameraState(m_focusedModelId,
                                     m_uiManager->viewportPanel()->getCameraState());
    }

    ++m_loadingState.generation;
    m_loadingState.reset();
    m_focusedModelId = -1;
    m_focusedModelIsLibraryPreview = false;

    if (m_workspace)
        m_workspace->clearAll();
    if (m_uiManager) {
        if (auto* panel = m_uiManager->viewportPanel()) {
            panel->setMaterialTexture(nullptr);
            panel->setMesh(nullptr);
            panel->setPresentationIdentity(viewport::PresentationIdentity::none());
        }
        if (auto* panel = m_uiManager->propertiesPanel()) {
            panel->clearMesh();
            panel->clearMaterial();
        }
        if (auto* panel = m_uiManager->materialsPanel())
            panel->setModelLoaded(false);
        if (auto* panel = m_uiManager->projectPanel())
            panel->clearSelection();
        if (auto* panel = m_uiManager->gcodePanel())
            panel->clear();
        if (auto* panel = m_uiManager->cutOptimizerPanel())
            panel->clear();
        if (auto* panel = m_uiManager->directCarvePanel())
            panel->clearProjectContext();
    }
    m_activeMaterialTexture.reset();
    m_activeMaterialId = -1;
}

uint64_t Application::projectSessionGeneration() const {
    return m_projectSession ? m_projectSession->snapshot().generation.value : 0;
}

std::optional<int64_t> Application::activeProjectIdentity() const {
    if (!m_projectSession || !m_projectSession->snapshot().activeProject.has_value())
        return std::nullopt;
    return m_projectSession->snapshot().activeProject->value;
}

bool Application::modelLoadStillCurrent(uint64_t loadGeneration,
                                        std::optional<int64_t> projectIdentity,
                                        int64_t modelId,
                                        ModelLoadPurpose purpose) const {
    return loadGeneration == m_loadingState.generation.load() &&
           projectIdentity == activeProjectIdentity() &&
           (purpose == ModelLoadPurpose::LibraryPreview || modelId == m_focusedModelId);
}

} // namespace dw
