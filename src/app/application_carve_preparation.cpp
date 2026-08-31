// Application adapter for beginning one exact, project-pinned carve preparation.

#include "app/application.h"

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "app/carve_preparation_adapter.h"
#include "core/database/model_repository.h"
#include "core/project/project.h"
#include "managers/ui_manager.h"
#include "modules/project_session/project_session.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

ProjectOpenItem newDirectCarveOperation(const ProjectOpenItem& model,
                                        const ModelRecord& source) {
    ProjectOpenItem operation;
    operation.projectId = model.projectId;
    operation.itemType = ProjectOpenItemType::Operation;
    operation.sourceTable = "direct_carve";
    operation.sourceKey = "direct_carve:model_item:" + std::to_string(model.id);
    operation.parentItemId = model.id;
    operation.status = ProjectOpenItemStatus::Planned;
    // User-facing label says CAM; identifiers keep the direct_carve keys so
    // existing projects resume against the same operation rows.
    operation.displayName = "CAM: " + source.name;
    operation.intentJson = nlohmann::json{
        {"operation_kind", "direct_carve"},
        {"description", "Prepared from this exact project design."},
        {"model_name", source.name},
        {"model_source_path", source.filePath.string()},
    }.dump();
    operation.snapshotJson = nlohmann::json{
        {"setup",
         {{"model_loaded", false},
          {"material_selected", false},
          {"finishing_tool_selected", false},
          {"toolpath_generated", false}}},
    }.dump();
    return operation;
}

const char* preparationFailure(PrepareCarveAdapterStatus status) {
    switch (status) {
    case PrepareCarveAdapterStatus::AmbiguousOperation:
        return "This design has more than one carve setup. Open the setup you want to continue.";
    case PrepareCarveAdapterStatus::InvalidHierarchy:
        return "The carve setup is no longer attached to this design.";
    case PrepareCarveAdapterStatus::InvalidModelSource:
        return "The project design no longer points to a usable Library model.";
    case PrepareCarveAdapterStatus::TargetUnavailable:
        return "The selected project item changed or needs repair.";
    case PrepareCarveAdapterStatus::PolicyRejected:
        return "The project or preparation identity changed. Refresh the Project Plan.";
    default:
        return "The carve setup could not be opened safely.";
    }
}

} // namespace

bool Application::beginPrepareCarve(workshop::ProjectItemRef target) {
    if (!m_projectManager || !m_projectSession || !m_modelRepo || !target.valid())
        return false;

    const auto context = m_projectSession->snapshot();
    const auto activeProject = context.activeProject;
    auto items = m_projectManager->currentOpenItems();
    const auto token = carve_preparation::PreparationToken{m_nextPreparationToken++};
    const auto revision = carve_preparation::PreparationRevision{context.generation.value};
    auto result = resolvePrepareCarvePin(activeProject, target, token, revision, items);

    if (result.status == PrepareCarveAdapterStatus::CreateProjectRequired) {
        showHome(true);
        return false;
    }

    if (result.status == PrepareCarveAdapterStatus::OperationRequired) {
        const auto model = m_projectManager->findOpenItem(target.item.value);
        if (!model || !activeProject || model->projectId != activeProject->value ||
            model->itemType != ProjectOpenItemType::Model || !model->sourceId) {
            return false;
        }
        const auto source = m_modelRepo->findById(*model->sourceId);
        if (!source) return false;

        const auto operationId =
            m_projectManager->upsertOpenItem(newDirectCarveOperation(*model, *source));
        if (!operationId) {
            ToastManager::instance().show(
                ToastType::Error, "Prepare Carve", "The planned carve setup could not be saved.");
            return false;
        }
        items = m_projectManager->currentOpenItems();
        target = {*activeProject, workshop::ProjectItemId(*operationId)};
        result = resolvePrepareCarvePin(activeProject, target, token, revision, items);
    }

    if (result.status != PrepareCarveAdapterStatus::Ready || !result.pin) {
        ToastManager::instance().show(
            ToastType::Warning, "Prepare Carve Unavailable", preparationFailure(result.status));
        return false;
    }

    const auto operation = m_projectManager->findOpenItem(result.pin->operationItem().item.value);
    if (!operation || operation->projectId != result.pin->project().value)
        return false;
    return activateProjectOpenItem(*operation) != ProjectItemActivationStatus::Rejected;
}

} // namespace dw
