#include "app/carve_preparation_adapter.h"

#include <algorithm>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace dw {
namespace {

bool usable(const ProjectOpenItem& item) {
    return item.status != ProjectOpenItemStatus::Missing &&
           item.status != ProjectOpenItemStatus::Stale;
}

bool directCarveOperation(const ProjectOpenItem& item) {
    if (item.itemType != ProjectOpenItemType::Operation) return false;
    const auto intent = nlohmann::json::parse(item.intentJson, nullptr, false);
    return intent.is_object() &&
           intent.value("operation_kind", std::string()) == "direct_carve";
}

const ProjectOpenItem* findExact(const std::vector<ProjectOpenItem>& items,
                                 workshop::ProjectItemRef ref) {
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.id == ref.item.value && item.projectId == ref.project.value;
    });
    return found == items.end() ? nullptr : &*found;
}

PrepareCarveAdapterResult rejected(PrepareCarveAdapterStatus status) {
    return {status, std::nullopt, carve_preparation::PreparationIdentityIssue::None};
}

} // namespace

carve_preparation::PreparationIdentitySnapshot makePreparationIdentitySnapshot(
    std::optional<workshop::ProjectId> activeProject,
    carve_preparation::PreparationRevision revision,
    const std::vector<ProjectOpenItem>& items) {
    std::vector<carve_preparation::PreparationItemSnapshot> snapshots;
    if (activeProject) {
        for (const auto& item : items) {
            if (item.projectId != activeProject->value) continue;
            const workshop::ProjectItemRef ref{*activeProject,
                                               workshop::ProjectItemId(item.id)};
            if (item.itemType == ProjectOpenItemType::Model) {
                std::optional<workshop::LibraryItemRef> source;
                if (item.sourceTable == "models" && item.sourceId && *item.sourceId > 0) {
                    source = workshop::LibraryItemRef{
                        workshop::LibraryItemKind::Model,
                        workshop::LibraryItemId(*item.sourceId)};
                }
                snapshots.emplace_back(ref,
                                       carve_preparation::PreparationItemKind::Model,
                                       std::nullopt,
                                       source);
            } else if (item.itemType == ProjectOpenItemType::Operation) {
                std::optional<workshop::ProjectItemId> parent;
                if (item.parentItemId)
                    parent = workshop::ProjectItemId(*item.parentItemId);
                snapshots.emplace_back(ref,
                                       carve_preparation::PreparationItemKind::Operation,
                                       parent,
                                       std::nullopt);
            }
        }
    }
    return {activeProject, revision, std::move(snapshots)};
}

PrepareCarveAdapterResult resolvePrepareCarvePin(
    std::optional<workshop::ProjectId> activeProject,
    workshop::ProjectItemRef target,
    carve_preparation::PreparationToken token,
    carve_preparation::PreparationRevision revision,
    const std::vector<ProjectOpenItem>& items) {
    if (!activeProject) {
        const auto decision = carve_preparation::PreparationIdentityPolicy::evaluate(
            true,
            std::nullopt,
            makePreparationIdentitySnapshot(activeProject, revision, items));
        return {PrepareCarveAdapterStatus::CreateProjectRequired,
                std::nullopt,
                decision.issue};
    }
    if (!target.valid() || target.project != *activeProject)
        return rejected(PrepareCarveAdapterStatus::TargetUnavailable);

    const auto* targetItem = findExact(items, target);
    if (!targetItem || !usable(*targetItem))
        return rejected(PrepareCarveAdapterStatus::TargetUnavailable);

    const ProjectOpenItem* model = nullptr;
    const ProjectOpenItem* operation = nullptr;
    if (targetItem->itemType == ProjectOpenItemType::Model) {
        model = targetItem;
        std::vector<const ProjectOpenItem*> operations;
        for (const auto& item : items) {
            if (item.projectId == activeProject->value && item.parentItemId == model->id &&
                directCarveOperation(item)) {
                operations.push_back(&item);
            }
        }
        if (operations.empty())
            return rejected(PrepareCarveAdapterStatus::OperationRequired);
        if (operations.size() != 1)
            return rejected(PrepareCarveAdapterStatus::AmbiguousOperation);
        if (!usable(*operations.front()))
            return rejected(PrepareCarveAdapterStatus::TargetUnavailable);
        operation = operations.front();
    } else if (directCarveOperation(*targetItem)) {
        operation = targetItem;
        if (!operation->parentItemId)
            return rejected(PrepareCarveAdapterStatus::InvalidHierarchy);
        const workshop::ProjectItemRef parentRef{
            *activeProject, workshop::ProjectItemId(*operation->parentItemId)};
        model = findExact(items, parentRef);
        if (!model || !usable(*model) || model->itemType != ProjectOpenItemType::Model)
            return rejected(PrepareCarveAdapterStatus::InvalidHierarchy);
    } else {
        return rejected(PrepareCarveAdapterStatus::TargetUnavailable);
    }

    if (model->sourceTable != "models" || !model->sourceId || *model->sourceId <= 0)
        return rejected(PrepareCarveAdapterStatus::InvalidModelSource);

    const workshop::ProjectItemRef modelRef{
        *activeProject, workshop::ProjectItemId(model->id)};
    const workshop::ProjectItemRef operationRef{
        *activeProject, workshop::ProjectItemId(operation->id)};
    const workshop::LibraryItemRef modelSource{
        workshop::LibraryItemKind::Model,
        workshop::LibraryItemId(*model->sourceId)};
    carve_preparation::PrepareCarvePin pin(
        *activeProject, modelRef, modelSource, operationRef, token, revision);
    const auto decision = carve_preparation::PreparationIdentityPolicy::evaluate(
        true,
        pin,
        makePreparationIdentitySnapshot(activeProject, revision, items));
    if (decision.status != carve_preparation::PreparationIdentityStatus::Ready) {
        return {PrepareCarveAdapterStatus::PolicyRejected, std::nullopt, decision.issue};
    }
    return {PrepareCarveAdapterStatus::Ready, std::move(pin), decision.issue};
}

} // namespace dw
