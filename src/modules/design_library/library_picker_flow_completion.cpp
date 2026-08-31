#include "library_picker_flow.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace dw::design_library {
namespace {

bool sameItem(workshop::LibraryItemRef lhs, workshop::LibraryItemRef rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

bool contains(const std::vector<workshop::LibraryItemRef>& items,
              workshop::LibraryItemRef candidate) noexcept {
    return std::any_of(items.begin(), items.end(), [candidate](const auto item) {
        return sameItem(item, candidate);
    });
}

} // namespace

LibraryPickerTransition LibraryPickerFlow::handle(
    const CompleteLibraryPreview& intent, const workshop::WorkshopContextSnapshot& projectSession) {
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
    if (!m_snapshot.pendingPreviewToken.has_value() ||
        *m_snapshot.pendingPreviewToken != intent.token) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::StaleCompletion);
    }

    if (intent.presented)
        m_snapshot.previewItem = m_snapshot.pendingPreviewItem;
    m_snapshot.pendingPreviewItem.reset();
    m_snapshot.pendingPreviewToken.reset();
    return transition(LibraryPickerTransitionStatus::Applied);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const CompleteLibraryAdd& intent, const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (m_snapshot.purpose != LibraryPickerPurpose::AddToProject ||
        m_snapshot.startRequestPending) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::WrongPurpose);
    }
    if (!contextMatches(projectSession))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ContextChanged);
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::PickerNotVisible);
    if (!m_snapshot.pendingActionToken.has_value() ||
        *m_snapshot.pendingActionToken != intent.token) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::StaleCompletion);
    }
    if (!m_snapshot.activeProject.has_value() || intent.project != *m_snapshot.activeProject) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ProjectMismatch);
    }

    std::vector<workshop::LibraryItemRef> added;
    added.reserve(intent.addedItems.size());
    for (const auto item : intent.addedItems) {
        if (!item.valid() || !m_snapshot.isAddPending(item))
            return transition(LibraryPickerTransitionStatus::Rejected,
                              LibraryPickerTransitionReason::StaleCompletion);
        if (!contains(added, item))
            added.push_back(item);
    }
    for (const auto item : added) {
        if (!m_snapshot.isProjectMember(item))
            m_snapshot.projectMembership.push_back(item);
    }
    m_snapshot.pendingAddItems.clear();
    m_snapshot.pendingActionToken.reset();
    return transition(LibraryPickerTransitionStatus::Applied);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const CompleteStartProject& intent, const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (m_snapshot.purpose != LibraryPickerPurpose::StartProject)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::WrongPurpose);
    if (!m_snapshot.startRequestPending || !m_snapshot.pendingActionToken.has_value() ||
        *m_snapshot.pendingActionToken != intent.token) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::StaleCompletion);
    }

    if (intent.createdProject.has_value()) {
        const bool createdProjectIsNew = intent.createdProject->valid() &&
                                         (!m_snapshot.activeProject.has_value() ||
                                          *intent.createdProject != *m_snapshot.activeProject);
        const bool createdProjectIsActive =
            projectSession.route == workshop::WorkshopRoute::Project &&
            projectSession.activeProject == intent.createdProject && !projectSession.runLocked();
        if (!createdProjectIsNew || !createdProjectIsActive)
            return transition(LibraryPickerTransitionStatus::Rejected,
                              LibraryPickerTransitionReason::ContextChanged);
        m_snapshot = LibraryPickerSnapshot{};
        return transition(LibraryPickerTransitionStatus::Applied);
    }

    if (!contextMatches(projectSession))
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::ContextChanged);
    if (projectSession.route != workshop::WorkshopRoute::DesignLibrary)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::PickerNotVisible);
    m_snapshot.startRequestPending = false;
    m_snapshot.pendingActionToken.reset();
    return transition(LibraryPickerTransitionStatus::Applied);
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const CancelLibraryPicker& /*intent*/,
    const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);

    const bool safeToRestore = contextMatches(projectSession) &&
                               projectSession.route == workshop::WorkshopRoute::DesignLibrary;
    if (!safeToRestore) {
        m_snapshot = LibraryPickerSnapshot{};
        return transition(LibraryPickerTransitionStatus::Applied,
                          LibraryPickerTransitionReason::ContextChanged);
    }
    if (m_snapshot.pendingActionToken.has_value() || m_snapshot.returnPending)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::RequestPending);

    const auto token = nextToken();
    m_snapshot.returnPending = true;
    m_snapshot.pendingRestoreToken = token;
    m_snapshot.pendingRestoreGeneration = projectSession.generation;
    m_snapshot.pendingPreviewItem.reset();
    m_snapshot.pendingPreviewToken.reset();
    auto result = transition(LibraryPickerTransitionStatus::RequestIssued);
    result.request = RestoreLibraryContextRequest{token,
                                                  projectSession.generation,
                                                  m_snapshot.returnRoute,
                                                  m_snapshot.activeProject,
                                                  m_snapshot.returnProjectItem};
    return result;
}

LibraryPickerTransition LibraryPickerFlow::handle(
    const CompleteLibraryRestore& intent, const workshop::WorkshopContextSnapshot& projectSession) {
    if (!m_snapshot.active)
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::NotActive);
    if (!m_snapshot.returnPending || !m_snapshot.pendingRestoreToken.has_value() ||
        *m_snapshot.pendingRestoreToken != intent.token) {
        return transition(LibraryPickerTransitionStatus::Rejected,
                          LibraryPickerTransitionReason::StaleCompletion);
    }

    if (intent.restored && returnContextMatches(projectSession)) {
        m_snapshot = LibraryPickerSnapshot{};
        return transition(LibraryPickerTransitionStatus::Applied);
    }

    const bool pickerContextIntact = contextMatches(projectSession) &&
                                     projectSession.route == workshop::WorkshopRoute::DesignLibrary;
    if (pickerContextIntact) {
        m_snapshot.returnPending = false;
        m_snapshot.pendingRestoreToken.reset();
        m_snapshot.pendingRestoreGeneration.reset();
        return transition(intent.restored ? LibraryPickerTransitionStatus::Rejected
                                          : LibraryPickerTransitionStatus::Applied,
                          intent.restored ? LibraryPickerTransitionReason::ContextChanged
                                          : LibraryPickerTransitionReason::None);
    }

    m_snapshot = LibraryPickerSnapshot{};
    return transition(LibraryPickerTransitionStatus::Applied,
                      LibraryPickerTransitionReason::ContextChanged);
}

bool LibraryPickerFlow::returnContextMatches(
    const workshop::WorkshopContextSnapshot& projectSession) const noexcept {
    if (!m_snapshot.pendingRestoreGeneration.has_value() ||
        m_snapshot.pendingRestoreGeneration->value == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const bool exactSuccessor = projectSession.generation.value ==
                                m_snapshot.pendingRestoreGeneration->value + 1;
    return !projectSession.runLocked() && projectSession.route == m_snapshot.returnRoute &&
           projectSession.activeProject == m_snapshot.activeProject &&
           projectSession.activeProjectItem == m_snapshot.returnProjectItem && exactSuccessor &&
           !projectSession.libraryPreview.has_value() &&
           !projectSession.libraryReturnRoute.has_value();
}

} // namespace dw::design_library
