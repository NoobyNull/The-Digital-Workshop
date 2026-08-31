// Typed Design Library intent execution. The panel emits presentation intents;
// this composition layer coordinates preview loading and durable services.

#include "app/application.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "app/library_workflow_coordinator.h"
#include "app/library_workflow_adapter.h"
#include "core/config/config.h"
#include "core/database/gcode_repository.h"
#include "core/library/library_manager.h"
#include "core/materials/material_manager.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/project/project_asset_membership.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_presentation.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_workshop_controller.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

namespace library = design_library;
using library_workflow_adapter::accepted;
using library_workflow_adapter::deletionSource;
using library_workflow_adapter::durableItems;
using library_workflow_adapter::flowAccepted;
using library_workflow_adapter::membershipFailure;
using library_workflow_adapter::pending;
using library_workflow_adapter::pickerFailureMessage;
using library_workflow_adapter::previewStillCurrent;
using library_workflow_adapter::projectAsset;
using library_workflow_adapter::projectBlockMessage;
using library_workflow_adapter::rejected;
using library_workflow_adapter::sameLibraryItem;

struct ProjectStartProbe {
    bool callbackRan = false;
    bool initialCallReturned = false;
    bool succeeded = false;
    std::string message;
};

} // namespace

void Application::wireLibraryPanel() {
    auto* panel = m_uiManager ? m_uiManager->libraryPanel() : nullptr;
    if (!panel || !m_libraryWorkflow)
        return;

    panel->setOnRegenerateThumbnail(
        [this](const std::vector<int64_t>& ids) { regenerateThumbnails(ids); });
    panel->setOnAssignDefaultMaterial([this](int64_t modelId) {
        const i64 materialId = Config::instance().getDefaultMaterialId();
        if (materialId > 0 && m_materialManager && m_materialManager->getMaterial(materialId))
            m_materialManager->assignMaterialToModel(materialId, modelId);
    });

    panel->setOnLibraryIntent([this](const library::LibraryPanelIntent& intent)
                                  -> library::LibraryPanelIntentResult {
        return std::visit(
            [this](const auto& typed) -> library::LibraryPanelIntentResult {
                using Intent = std::decay_t<decltype(typed)>;
                auto* currentPanel = m_uiManager ? m_uiManager->libraryPanel() : nullptr;
                if (!currentPanel || !m_libraryWorkflow || !m_projectSession)
                    return rejected("The Design Library is unavailable.");
                auto& flow = m_libraryWorkflow->picker();

                if constexpr (std::is_same_v<Intent, library::LibrarySelectionChanged>) {
                    const auto transition = flow.dispatch(
                        library::ReplaceLibrarySelection{typed.items},
                        m_projectSession->snapshot());
                    const std::string error =
                        flowAccepted(transition) ? std::string{}
                                                 : pickerFailureMessage(transition.reason);
                    currentPanel->setPickerState(transition.snapshot, error);
                    if (flowAccepted(transition) && typed.items.size() == 1 &&
                        typed.items.front().kind == workshop::LibraryItemKind::Model &&
                        m_libraryManager && m_uiManager->propertiesPanel()) {
                        const auto model =
                            m_libraryManager->getModel(typed.items.front().item.value);
                        if (model)
                            m_uiManager->propertiesPanel()->setModelRecord(*model);
                    }
                    return flowAccepted(transition) ? accepted() : rejected(error);
                }

                if constexpr (std::is_same_v<Intent, library::LibraryPreviewRequested>) {
                    if (m_loadingState.active.load()) {
                        return rejected(
                            "Wait for the current model preview to finish loading first.");
                    }
                    const auto transition = flow.dispatch(
                        library::RequestLibraryPreview{typed.item},
                        m_projectSession->snapshot());
                    if (!flowAccepted(transition)) {
                        const auto error = pickerFailureMessage(transition.reason);
                        currentPanel->setPickerState(transition.snapshot, error);
                        return rejected(error);
                    }
                    if (!transition.request) {
                        currentPanel->setPickerState(transition.snapshot);
                        return accepted();
                    }
                    const auto* request =
                        std::get_if<library::PreviewLibraryItemRequest>(&*transition.request);
                    if (!request)
                        return rejected("The preview request was invalid.");
                    currentPanel->setPickerState(transition.snapshot);

                    if (request->item.kind != workshop::LibraryItemKind::Model &&
                        request->item.kind != workshop::LibraryItemKind::GCode) {
                        const auto completed = flow.dispatch(
                            library::CompleteLibraryPreview{request->token, false},
                            m_projectSession->snapshot());
                        const std::string error =
                            "The preview selection contained an invalid item.";
                        currentPanel->setPickerState(completed.snapshot, error);
                        return rejected(error);
                    }

                    if (request->item.kind == workshop::LibraryItemKind::Model) {
                        const auto requestCopy = *request;
                        onModelSelected(
                            requestCopy.item.item.value,
                            [this, requestCopy](ModelSelectionStatus status) {
                                if (!m_libraryWorkflow || !m_projectSession ||
                                    !m_projectWorkshopController)
                                    return;
                                bool presented = false;
                                std::string error;
                                if (status == ModelSelectionStatus::Loaded &&
                                    previewStillCurrent(m_libraryWorkflow.get(),
                                                        m_projectSession.get(),
                                                        requestCopy.token,
                                                        requestCopy.item)) {
                                    const auto context = m_projectSession->snapshot();
                                    const auto preview = m_projectWorkshopController->dispatch(
                                        workshop::PreviewLibraryItemIntent{
                                            libraryExperienceMode(),
                                            context.generation,
                                            requestCopy.item});
                                    presented = preview.accepted();
                                    if (!presented)
                                        error = "The Library context changed before preview opened.";
                                } else if (status == ModelSelectionStatus::Failed) {
                                    error = "That model could not be loaded for preview.";
                                }
                                if (!presented && m_uiManager &&
                                    m_uiManager->viewportPanel()) {
                                    m_uiManager->viewportPanel()->setPresentationIdentity(
                                        viewport::PresentationIdentity::none());
                                }
                                const auto completed = m_libraryWorkflow->picker().dispatch(
                                    library::CompleteLibraryPreview{requestCopy.token, presented},
                                    m_projectSession->snapshot());
                                if (auto* livePanel =
                                        m_uiManager ? m_uiManager->libraryPanel() : nullptr) {
                                    livePanel->setPickerState(completed.snapshot, error);
                                }
                            },
                            [this, requestCopy]() {
                                return previewStillCurrent(m_libraryWorkflow.get(),
                                                           m_projectSession.get(),
                                                           requestCopy.token,
                                                           requestCopy.item);
                            },
                            ModelLoadPurpose::LibraryPreview);
                        return pending();
                    }

                    const auto record = m_gcodeRepo
                                            ? m_gcodeRepo->findById(request->item.item.value)
                                            : std::nullopt;
                    auto* gcodePanel = m_uiManager->gcodePanel();
                    bool loaded = record && gcodePanel &&
                                  gcodePanel->loadFile(PathResolver::resolve(
                                      record->filePath, PathCategory::GCode).string());
                    bool presented = false;
                    if (loaded && previewStillCurrent(m_libraryWorkflow.get(),
                                                      m_projectSession.get(),
                                                      request->token,
                                                      request->item)) {
                        const auto context = m_projectSession->snapshot();
                        presented = m_projectWorkshopController
                                        ->dispatch(workshop::PreviewLibraryItemIntent{
                                            libraryExperienceMode(),
                                            context.generation,
                                            request->item})
                                        .accepted();
                    }
                    const auto completed = flow.dispatch(
                        library::CompleteLibraryPreview{request->token, presented},
                        m_projectSession->snapshot());
                    if (auto* viewportPanel =
                            m_uiManager ? m_uiManager->viewportPanel() : nullptr) {
                        if (presented && record) {
                            std::string projectLabel;
                            const auto project =
                                m_projectManager ? m_projectManager->currentProject() : nullptr;
                            if (project)
                                projectLabel = project->name();
                            viewportPanel->setPresentationIdentity(
                                viewport::PresentationIdentity::libraryPreview(
                                    std::move(projectLabel), record->name));
                        } else if (loaded) {
                            viewportPanel->setPresentationIdentity(
                                viewport::PresentationIdentity::none());
                        }
                    }
                    const std::string error =
                        presented ? std::string{} : "That G-code could not be loaded for preview.";
                    currentPanel->setPickerState(completed.snapshot, error);
                    return presented ? accepted() : rejected(error);
                }

                if constexpr (std::is_same_v<Intent,
                                             library::LibraryPrimaryActionRequested>) {
                    const auto transition = flow.dispatch(
                        library::ConfirmLibrarySelection{}, m_projectSession->snapshot());
                    if (!flowAccepted(transition) || !transition.request) {
                        const auto error = pickerFailureMessage(transition.reason);
                        currentPanel->setPickerState(transition.snapshot, error);
                        return rejected(error);
                    }
                    currentPanel->setPickerState(transition.snapshot);

                    if (const auto* start =
                            std::get_if<library::StartProjectWithLibraryItemRequest>(
                                &*transition.request)) {
                        const auto name = library::trimLibraryProjectName(typed.projectName);
                        if (name.empty()) {
                            const auto released = flow.dispatch(
                                library::CompleteStartProject{start->token, std::nullopt},
                                m_projectSession->snapshot());
                            currentPanel->setPickerState(released.snapshot,
                                                         "Enter a project name first.");
                            return rejected("Enter a project name first.");
                        }
                        const auto request = *start;
                        auto probe = std::make_shared<ProjectStartProbe>();
                        m_fileIOManager->newProject(
                            name,
                            [this, request, probe](bool created) {
                                probe->callbackRan = true;
                                if (!m_libraryWorkflow || !m_projectSession)
                                    return;
                                if (!created) {
                                    const auto released = m_libraryWorkflow->picker().dispatch(
                                        library::CompleteStartProject{request.token,
                                                                      std::nullopt},
                                        m_projectSession->snapshot());
                                    probe->message = "The project could not be created.";
                                    if (auto* livePanel =
                                            m_uiManager ? m_uiManager->libraryPanel() : nullptr) {
                                        livePanel->setPickerState(released.snapshot,
                                                                  probe->message);
                                    }
                                    return;
                                }

                                const auto project =
                                    m_projectManager ? m_projectManager->currentProject() : nullptr;
                                if (!project) {
                                    probe->message = "The created project could not be identified.";
                                    return;
                                }
                                const auto asset = projectAsset(request.item);
                                const auto membership = asset
                                                            ? m_libraryWorkflow->membership().ensure(
                                                                  {project->id(), {*asset}})
                                                            : ProjectAssetMembershipResult{};
                                const auto ensured = durableItems(membership);
                                const bool modelChosen = ensured.size() == 1 &&
                                                         sameLibraryItem(ensured.front(),
                                                                         request.item);
                                const auto completed = m_libraryWorkflow->picker().dispatch(
                                    library::CompleteStartProject{
                                        request.token, workshop::ProjectId(project->id())},
                                    m_projectSession->snapshot());
                                probe->succeeded = !completed.snapshot.active;
                                if (!probe->succeeded) {
                                    probe->message =
                                        "The project opened, but the Library task did not finish.";
                                    return;
                                }

                                m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
                                m_uiManager->showStartPage() = false;
                                const bool asynchronous = probe->initialCallReturned;
                                m_uiManager->showLibrary() = asynchronous;
                                m_deferredLibraryCloseFrames = asynchronous ? 2 : 0;
                                m_uiManager->showProject() = true;
                                m_uiManager->showViewport() = true;
                                m_uiManager->showProperties() = true;
                                m_uiManager->openWindow("project");
                                if (auto* livePanel = m_uiManager->libraryPanel())
                                    livePanel->clearPickerState();

                                if (modelChosen) {
                                    const auto item = m_projectManager->findOpenItemBySource(
                                        "models", request.item.item.value);
                                    if (item)
                                        (void)activateProjectOpenItem(*item, false);
                                    ToastManager::instance().show(
                                        ToastType::Success,
                                        "Project Started",
                                        "The selected model is ready in " + project->name() + ".");
                                } else {
                                    probe->message = membershipFailure(membership.failure);
                                    ToastManager::instance().show(
                                        ToastType::Warning,
                                        "Project Created, Model Not Chosen",
                                        probe->message +
                                            " Use Choose a model to try again.");
                                }
                            });
                        probe->initialCallReturned = true;
                        if (!probe->callbackRan)
                            return pending();
                        return probe->succeeded ? accepted() : rejected(probe->message);
                    }

                    const auto* add =
                        std::get_if<library::EnsureLibraryItemsInProjectRequest>(
                            &*transition.request);
                    if (!add)
                        return rejected("The Library action request was invalid.");
                    std::vector<ProjectAssetRef> assets;
                    assets.reserve(add->items.size());
                    bool validAssets = true;
                    for (const auto item : add->items) {
                        const auto asset = projectAsset(item);
                        if (!asset) {
                            validAssets = false;
                            break;
                        }
                        assets.push_back(*asset);
                    }
                    if (!validAssets) {
                        const auto released = flow.dispatch(
                            library::CompleteLibraryAdd{add->token, add->project, {}},
                            m_projectSession->snapshot());
                        const std::string error = "The Library selection contained an invalid item.";
                        currentPanel->setPickerState(released.snapshot, error);
                        return rejected(error);
                    }
                    const auto result = m_libraryWorkflow->membership().ensure(
                        {add->project.value, std::move(assets)});
                    const auto ensured = durableItems(result);
                    const bool complete = ensured.size() == add->items.size();
                    const auto completed = flow.dispatch(
                        library::CompleteLibraryAdd{add->token,
                                                    add->project,
                                                    complete ? ensured
                                                             : std::vector<workshop::LibraryItemRef>{}},
                        m_projectSession->snapshot());
                    if (!complete) {
                        const auto error = membershipFailure(result.failure);
                        currentPanel->setPickerState(completed.snapshot, error);
                        return rejected(error);
                    }
                    currentPanel->setPickerState(completed.snapshot);
                    ToastManager::instance().show(
                        ToastType::Success,
                        "Model Chosen",
                        "This project now uses the selected model.");
                    return accepted();
                }

                if constexpr (std::is_same_v<Intent, library::LibraryCancelRequested>) {
                    std::string error;
                    return requestLibraryReturn(error) ? accepted() : rejected(error);
                }

                if constexpr (std::is_same_v<Intent, library::LibraryDeleteRequested>) {
                    const auto snapshot = flow.snapshot();
                    if (!snapshot.active)
                        return library::LibraryDeleteResult{
                            library::LibraryDeleteResultStatus::Failed,
                            {},
                            "Open the Design Library before deleting an item."};
                    if (snapshot.pendingPreviewToken || snapshot.pendingActionToken ||
                        snapshot.returnPending) {
                        return library::LibraryDeleteResult{
                            library::LibraryDeleteResultStatus::Blocked,
                            {},
                            "Finish the current preview or project action before deleting."};
                    }

                    std::vector<LibrarySourceRef> sources;
                    sources.reserve(typed.items.size());
                    for (const auto item : typed.items) {
                        const auto source = deletionSource(item);
                        if (!source) {
                            return library::LibraryDeleteResult{
                                library::LibraryDeleteResultStatus::Failed,
                                {},
                                "The deletion selection contained an invalid item."};
                        }
                        sources.push_back(*source);
                    }
                    std::optional<LibrarySourceRef> activePreview;
                    const auto context = m_projectSession->snapshot();
                    if (context.libraryPreview) {
                        const auto source = deletionSource(*context.libraryPreview);
                        if (!source) {
                            return library::LibraryDeleteResult{
                                library::LibraryDeleteResultStatus::Blocked,
                                {},
                                "The active preview could not be identified safely."};
                        }
                        activePreview = *source;
                    }
                    const auto result =
                        m_libraryWorkflow->deletion().deleteSources(sources, activePreview);

                    std::vector<workshop::LibraryItemRef> confirmed;
                    for (const auto& outcome : result.items) {
                        if (!outcome.duplicate && outcome.selectionCanClear()) {
                            const auto requested = workshop::LibraryItemRef{
                                outcome.source.kind == LibrarySourceKind::Model
                                    ? workshop::LibraryItemKind::Model
                                    : workshop::LibraryItemKind::GCode,
                                workshop::LibraryItemId(outcome.source.id)};
                            confirmed.push_back(requested);
                        }
                    }
                    if (result.status == LibrarySourceDeletionStatus::PreflightRejected) {
                        const bool previewBlocked = std::any_of(
                            result.items.begin(), result.items.end(), [](const auto& item) {
                                return item.status ==
                                       LibrarySourceDeletionItemStatus::ActivePreview;
                            });
                        return library::LibraryDeleteResult{
                            library::LibraryDeleteResultStatus::Blocked,
                            {},
                            previewBlocked
                                ? "Return from this preview before deleting its Library source."
                                : projectBlockMessage(result.affectedProjects)};
                    }
                    if (result.status == LibrarySourceDeletionStatus::Deleted) {
                        return library::LibraryDeleteResult{
                            library::LibraryDeleteResultStatus::Deleted,
                            std::move(confirmed),
                            "Deleted from the Library."};
                    }
                    if (result.status == LibrarySourceDeletionStatus::PartiallyDeleted) {
                        return library::LibraryDeleteResult{
                            library::LibraryDeleteResultStatus::PartiallyDeleted,
                            std::move(confirmed),
                            "Some items were deleted; the remaining items were kept."};
                    }
                    return library::LibraryDeleteResult{
                        library::LibraryDeleteResultStatus::Failed,
                        {},
                        "Nothing was deleted. The Library sources were kept."};
                }
                return rejected("The Design Library received an unsupported action.");
            },
            intent);
    });

    wireTagDialog();
}

} // namespace dw
