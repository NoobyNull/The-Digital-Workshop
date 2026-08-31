#include "app/library_workflow_adapter.h"

#include <utility>

#include "app/library_workflow_coordinator.h"
#include "modules/project_session/project_session.h"

namespace dw::library_workflow_adapter {

design_library::LibraryActionResult accepted(std::string message) {
    return {design_library::LibraryActionResultStatus::Accepted, std::move(message)};
}

design_library::LibraryActionResult pending() {
    return {design_library::LibraryActionResultStatus::Pending, {}};
}

design_library::LibraryActionResult rejected(std::string message) {
    return {design_library::LibraryActionResultStatus::Rejected, std::move(message)};
}

bool sameLibraryItem(workshop::LibraryItemRef lhs,
                     workshop::LibraryItemRef rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

bool flowAccepted(const design_library::LibraryPickerTransition& transition) noexcept {
    using Status = design_library::LibraryPickerTransitionStatus;
    return transition.status == Status::Applied || transition.status == Status::Unchanged ||
           transition.status == Status::RequestIssued;
}

std::string pickerFailureMessage(
    design_library::LibraryPickerTransitionReason reason) {
    using Reason = design_library::LibraryPickerTransitionReason;
    switch (reason) {
    case Reason::NoActiveProject:
        return "Open a project before choosing its model.";
    case Reason::SelectionRequired:
        return "Choose one model first.";
    case Reason::SingleModelRequired:
        return "Choose exactly one model.";
    case Reason::ProjectAlreadyHasDesign:
        return "This project already has a model. Start a new project to use a different model.";
    case Reason::AlreadyMember:
        return "That model is already chosen for this project.";
    case Reason::RequestPending:
        return "Finish the current Library action first.";
    case Reason::ContextChanged:
    case Reason::ProjectMismatch:
        return "The active project changed. Return to the project and try again.";
    case Reason::ItemNotSelected:
        return "Select that item before previewing it.";
    case Reason::InvalidContext:
        return "The Design Library cannot open from the current workspace state.";
    case Reason::AlreadyActive:
        return "Finish the current Design Library task first.";
    case Reason::PickerNotVisible:
        return "Reopen the Design Library and try again.";
    default:
        return "The Design Library could not complete that action.";
    }
}

std::optional<ProjectAssetRef> projectAsset(workshop::LibraryItemRef item) {
    if (!item.valid())
        return std::nullopt;
    switch (item.kind) {
    case workshop::LibraryItemKind::Model:
        return ProjectAssetRef{ProjectAssetKind::Model, item.item.value};
    case workshop::LibraryItemKind::GCode:
        return ProjectAssetRef{ProjectAssetKind::GCode, item.item.value};
    }
    return std::nullopt;
}

namespace {

std::optional<workshop::LibraryItemRef> libraryItem(ProjectAssetRef item) {
    if (item.sourceId <= 0)
        return std::nullopt;
    switch (item.kind) {
    case ProjectAssetKind::Model:
        return workshop::LibraryItemRef{workshop::LibraryItemKind::Model,
                                        workshop::LibraryItemId(item.sourceId)};
    case ProjectAssetKind::GCode:
        return workshop::LibraryItemRef{workshop::LibraryItemKind::GCode,
                                        workshop::LibraryItemId(item.sourceId)};
    }
    return std::nullopt;
}

bool durableOutcome(ProjectAssetMembershipItemStatus status) noexcept {
    return status == ProjectAssetMembershipItemStatus::Added ||
           status == ProjectAssetMembershipItemStatus::AlreadyMember;
}

} // namespace

std::vector<workshop::LibraryItemRef> durableItems(
    const ProjectAssetMembershipResult& result) {
    std::vector<workshop::LibraryItemRef> items;
    if (result.status == ProjectAssetMembershipStatus::Rejected)
        return items;
    for (const auto& outcome : result.items) {
        if (durableOutcome(outcome.status)) {
            const auto item = libraryItem(outcome.asset);
            if (item)
                items.push_back(*item);
        }
    }
    return items;
}

std::string membershipFailure(ProjectAssetMembershipFailure failure) {
    using Failure = ProjectAssetMembershipFailure;
    switch (failure) {
    case Failure::ModelLimitExceeded:
        return "A project can use one model. Start a new project to use a different model.";
    case Failure::ProjectMismatch:
    case Failure::ActiveProjectChanged:
        return "The active project changed before the model could be chosen.";
    case Failure::SourceMissing:
    case Failure::SourceFileMissing:
        return "A selected Library source is missing or unavailable.";
    case Failure::StorageUnavailable:
    case Failure::DirectoryPublishFailed:
        return "The project folder is not ready for new items.";
    case Failure::TransactionUnavailable:
    case Failure::TransactionCommitFailed:
    case Failure::RollbackFailed:
        return "The project database could not safely commit the selected items.";
    default:
        return "The model could not be chosen for this project.";
    }
}

bool previewStillCurrent(const LibraryWorkflowCoordinator* workflow,
                         const workshop::ProjectSession* session,
                         design_library::LibraryPickerRequestToken token,
                         workshop::LibraryItemRef item) {
    if (!workflow || !session)
        return false;
    const auto picker = workflow->picker().snapshot();
    const auto context = session->snapshot();
    return picker.active && picker.pendingPreviewToken == token &&
           picker.pendingPreviewItem.has_value() &&
           sameLibraryItem(*picker.pendingPreviewItem, item) &&
           context.route == workshop::WorkshopRoute::DesignLibrary &&
           context.activeProject == picker.activeProject &&
           context.activeProjectItem == picker.returnProjectItem && !context.runLocked();
}

std::optional<LibrarySourceRef> deletionSource(workshop::LibraryItemRef item) {
    if (!item.valid())
        return std::nullopt;
    switch (item.kind) {
    case workshop::LibraryItemKind::Model:
        return LibrarySourceRef{LibrarySourceKind::Model, item.item.value};
    case workshop::LibraryItemKind::GCode:
        return LibrarySourceRef{LibrarySourceKind::GCode, item.item.value};
    }
    return std::nullopt;
}

std::string projectBlockMessage(
    const std::vector<LibrarySourceProjectRef>& projects) {
    if (projects.empty())
        return "This Library source cannot be deleted right now.";
    std::string names;
    for (std::size_t index = 0; index < projects.size(); ++index) {
        if (index > 0)
            names += index + 1 == projects.size() ? " and " : ", ";
        names += projects[index].name;
    }
    return "Remove this source from " + names + " before deleting it from the Library.";
}

} // namespace dw::library_workflow_adapter
