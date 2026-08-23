#include "library_picker_flow.h"

#include <algorithm>

namespace dw::design_library {
namespace {

bool sameItem(workshop::LibraryItemRef lhs, workshop::LibraryItemRef rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

bool contains(const std::vector<workshop::LibraryItemRef>& items,
              workshop::LibraryItemRef candidate) noexcept {
    return std::any_of(items.begin(), items.end(), [candidate](const auto& item) {
        return sameItem(item, candidate);
    });
}

bool normalizeItems(const std::vector<workshop::LibraryItemRef>& input,
                    std::vector<workshop::LibraryItemRef>& output) {
    output.clear();
    output.reserve(input.size());
    for (const auto item : input) {
        if (!item.valid())
            return false;
        if (!contains(output, item))
            output.push_back(item);
    }
    return true;
}

bool sameItemSet(const std::vector<workshop::LibraryItemRef>& lhs,
                 const std::vector<workshop::LibraryItemRef>& rhs) noexcept {
    if (lhs.size() != rhs.size())
        return false;
    return std::all_of(lhs.begin(), lhs.end(), [&rhs](const auto item) {
        return contains(rhs, item);
    });
}

bool sameProjectItem(const std::optional<workshop::ProjectItemRef>& lhs,
                     const std::optional<workshop::ProjectItemRef>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value())
        return false;
    return !lhs.has_value() || *lhs == *rhs;
}

bool hasVisibleText(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") != std::string::npos;
}

bool choosesProjectModel(LibraryPickerPurpose purpose) noexcept {
    return purpose == LibraryPickerPurpose::StartProject ||
           purpose == LibraryPickerPurpose::AddToProject;
}

bool isSingleModelSelection(
    const std::vector<workshop::LibraryItemRef>& items) noexcept {
    return items.size() == 1 &&
           items.front().kind == workshop::LibraryItemKind::Model;
}

bool hasProjectModel(const LibraryPickerSnapshot& snapshot) noexcept {
    return std::any_of(snapshot.projectMembership.begin(),
                       snapshot.projectMembership.end(),
                       [](const auto item) {
                           return item.kind == workshop::LibraryItemKind::Model;
                       });
}

workshop::WorkshopRoute returnRouteFor(
    const workshop::WorkshopContextSnapshot& projectSession) noexcept {
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return projectSession.route;
    return projectSession.libraryReturnRoute.value_or(projectSession.activeProject.has_value()
                                                          ? workshop::WorkshopRoute::Project
                                                          : workshop::WorkshopRoute::Home);
}

} // namespace

bool LibraryPickerSnapshot::isProjectMember(workshop::LibraryItemRef item) const noexcept {
    return contains(projectMembership, item);
}

bool LibraryPickerSnapshot::isAddPending(workshop::LibraryItemRef item) const noexcept {
    return contains(pendingAddItems, item);
}

bool LibraryPickerSnapshot::canConfirm() const noexcept {
    if (!active || pendingActionToken.has_value() || returnPending)
        return false;
    if (purpose == LibraryPickerPurpose::StartProject)
        return isSingleModelSelection(selectedItems);
    if (purpose != LibraryPickerPurpose::AddToProject || !activeProject.has_value())
        return false;
    return isSingleModelSelection(selectedItems) &&
           !isProjectMember(selectedItems.front()) && !hasProjectModel(*this);
}

std::string LibraryPickerSnapshot::primaryActionLabel() const {
    switch (purpose) {
    case LibraryPickerPurpose::ManageLibrary:
        return {};
    case LibraryPickerPurpose::StartProject:
        return "Start project with this model";
    case LibraryPickerPurpose::AddToProject:
        return "Choose this model";
    }
    return {};
}

const LibraryPickerSnapshot& LibraryPickerFlow::snapshot() const noexcept {
    return m_snapshot;
}

LibraryPickerTransition LibraryPickerFlow::dispatch(
    const LibraryPickerIntent& intent, const workshop::WorkshopContextSnapshot& projectSession) {
    return std::visit([this, &projectSession](
                          const auto& value) { return handle(value, projectSession); },
                      intent);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const BeginLibraryPicker& intent, const workshop::WorkshopContextSnapshot& projectSession) {
    if (m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::AlreadyActive);
    if (projectSession.runLocked() || projectSession.route == workshop::WorkshopRoute::RunCnc)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidContext);
    if (projectSession.activeProjectItem.has_value() &&
        (!projectSession.activeProject.has_value() ||
         projectSession.activeProjectItem->project != *projectSession.activeProject)) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidContext);
    }

    std::vector<workshop::LibraryItemRef> membership;
    if (!normalizeItems(intent.projectMembership, membership) ||
        (!projectSession.activeProject.has_value() && !membership.empty())) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidContext);
    }
    if (intent.purpose == LibraryPickerPurpose::AddToProject &&
        !projectSession.activeProject.has_value()) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NoActiveProject);
    }
    if (intent.purpose == LibraryPickerPurpose::AddToProject &&
        !hasVisibleText(intent.activeProjectName)) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidContext);
    }

    const auto returnRoute = returnRouteFor(projectSession);
    if (returnRoute == workshop::WorkshopRoute::DesignLibrary ||
        returnRoute == workshop::WorkshopRoute::RunCnc) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidContext);
    }

    m_snapshot = LibraryPickerSnapshot{};
    m_snapshot.active = true;
    m_snapshot.purpose = intent.purpose;
    m_snapshot.activeProject = projectSession.activeProject;
    m_snapshot.activeProjectName = intent.activeProjectName;
    m_snapshot.returnRoute = returnRoute;
    m_snapshot.returnProjectItem = projectSession.activeProjectItem;
    m_snapshot.projectMembership = std::move(membership);
    return transition(LibraryPickerTransitionStatus::Applied);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const ReplaceLibrarySelection& intent,
    const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (m_snapshot.returnPending)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::RequestPending);
    if (!contextMatches(projectSession))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ContextChanged);
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::PickerNotVisible);

    std::vector<workshop::LibraryItemRef> selection;
    if (!normalizeItems(intent.items, selection))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidItem);
    if (choosesProjectModel(m_snapshot.purpose) && !selection.empty() &&
        !isSingleModelSelection(selection)) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::SingleModelRequired);
    }
    if (sameItemSet(selection, m_snapshot.selectedItems))
        return transition(LibraryPickerTransitionStatus::Unchanged);
    m_snapshot.selectedItems = std::move(selection);
    m_snapshot.pendingPreviewItem.reset();
    m_snapshot.pendingPreviewToken.reset();
    return transition(LibraryPickerTransitionStatus::Applied);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const RequestLibraryPreview& intent, const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (m_snapshot.returnPending)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::RequestPending);
    if (!contextMatches(projectSession))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ContextChanged);
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::PickerNotVisible);
    if (!intent.item.valid())
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidItem);
    if (!contains(m_snapshot.selectedItems, intent.item))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ItemNotSelected);
    if (m_snapshot.pendingPreviewItem.has_value() &&
        sameItem(*m_snapshot.pendingPreviewItem, intent.item)) {
        return transition(LibraryPickerTransitionStatus::Unchanged);
    }
    if (!m_snapshot.pendingPreviewItem.has_value() && m_snapshot.previewItem.has_value() &&
        sameItem(*m_snapshot.previewItem, intent.item)) {
        return transition(LibraryPickerTransitionStatus::Unchanged);
    }

    const auto token = nextToken();
    m_snapshot.pendingPreviewItem = intent.item;
    m_snapshot.pendingPreviewToken = token;
    auto result = transition(LibraryPickerTransitionStatus::RequestIssued);
    result.request = PreviewLibraryItemRequest{token, intent.item};
    return result;
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const OfferImportedLibraryItems& intent,
    const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (m_snapshot.returnPending)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::RequestPending);
    if (!contextMatches(projectSession))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ContextChanged);
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::PickerNotVisible);

    std::vector<workshop::LibraryItemRef> imported;
    if (!normalizeItems(intent.items, imported))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::InvalidItem);
    if (choosesProjectModel(m_snapshot.purpose)) {
        if (imported.empty())
            return transition(LibraryPickerTransitionStatus::Unchanged);
        if (!isSingleModelSelection(imported))
            return transition(LibraryPickerTransitionStatus::Rejected,
                              LibraryPickerTransitionReason::SingleModelRequired);
        if (sameItemSet(imported, m_snapshot.selectedItems))
            return transition(LibraryPickerTransitionStatus::Unchanged);
        m_snapshot.selectedItems = std::move(imported);
        m_snapshot.pendingPreviewItem.reset();
        m_snapshot.pendingPreviewToken.reset();
        return transition(LibraryPickerTransitionStatus::Applied);
    }
    const auto originalSize = m_snapshot.selectedItems.size();
    for (const auto item : imported) {
        if (!contains(m_snapshot.selectedItems, item))
            m_snapshot.selectedItems.push_back(item);
    }
    if (m_snapshot.selectedItems.size() == originalSize)
        return transition(LibraryPickerTransitionStatus::Unchanged);
    return transition(LibraryPickerTransitionStatus::Applied);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const ConfirmLibrarySelection& /*intent*/,
    const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (m_snapshot.returnPending)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::RequestPending);
    if (!contextMatches(projectSession))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ContextChanged);
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::PickerNotVisible);
    if (m_snapshot.pendingActionToken.has_value())
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::RequestPending);
    if (m_snapshot.purpose == LibraryPickerPurpose::ManageLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NoPrimaryAction);

    if (m_snapshot.purpose == LibraryPickerPurpose::StartProject) {
        if (m_snapshot.selectedItems.empty())
            return transition(LibraryPickerTransitionStatus::Rejected,
                              LibraryPickerTransitionReason::SelectionRequired);
        if (!isSingleModelSelection(m_snapshot.selectedItems)) {
            return transition(LibraryPickerTransitionStatus::Rejected,
                              LibraryPickerTransitionReason::SingleModelRequired);
        }
        const auto token = nextToken();
        m_snapshot.pendingActionToken = token;
        m_snapshot.startRequestPending = true;
        auto result = transition(LibraryPickerTransitionStatus::RequestIssued);
        result.request = StartProjectWithLibraryItemRequest{token,
                                                            m_snapshot.selectedItems.front()};
        return result;
    }

    if (!m_snapshot.activeProject.has_value())
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NoActiveProject);
    if (projectSession.activeProject != m_snapshot.activeProject) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ProjectMismatch);
    }
    if (m_snapshot.selectedItems.empty())
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::SelectionRequired);
    if (!isSingleModelSelection(m_snapshot.selectedItems))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::SingleModelRequired);

    const auto selected = m_snapshot.selectedItems.front();
    if (m_snapshot.isProjectMember(selected))
        return transition(LibraryPickerTransitionStatus::Unchanged,
                          LibraryPickerTransitionReason::AlreadyMember);
    if (hasProjectModel(m_snapshot))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ProjectAlreadyHasDesign);

    const auto token = nextToken();
    m_snapshot.pendingActionToken = token;
    m_snapshot.pendingAddItems = {selected};
    auto result = transition(LibraryPickerTransitionStatus::RequestIssued);
    result.request = EnsureLibraryItemsInProjectRequest{
        token, *m_snapshot.activeProject, {selected}};
    return result;
}

bool LibraryPickerFlow::contextMatches(
    const workshop::WorkshopContextSnapshot& projectSession) const noexcept {
    if (projectSession.runLocked() || projectSession.route == workshop::WorkshopRoute::RunCnc ||
        projectSession.activeProject != m_snapshot.activeProject ||
        !sameProjectItem(projectSession.activeProjectItem, m_snapshot.returnProjectItem)) {
        return false;
    }
    if (projectSession.route == workshop::WorkshopRoute::DesignLibrary &&
        projectSession.libraryReturnRoute.has_value() &&
        *projectSession.libraryReturnRoute != m_snapshot.returnRoute) {
        return false;
    }
    return true;
}

LibraryPickerRequestToken LibraryPickerFlow::nextToken() noexcept {
    return LibraryPickerRequestToken{m_nextTokenValue++};
}

LibraryPickerTransition LibraryPickerFlow::transition(LibraryPickerTransitionStatus status,
                                                      LibraryPickerTransitionReason reason) const {
    return LibraryPickerTransition{status, reason, m_snapshot, std::nullopt};
}

} // namespace dw::design_library
