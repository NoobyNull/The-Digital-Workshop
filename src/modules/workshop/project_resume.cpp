#include "project_resume.h"

#include <utility>

namespace dw::workshop {

ProjectResumeCoordinator::ProjectResumeCoordinator(ProjectResumeStore& store,
                                                   ProjectResumeCallbacks callbacks)
    : m_store(store), m_callbacks(std::move(callbacks)) {}

ProjectResumeResult ProjectResumeCoordinator::restore() {
    const auto loaded = m_store.load();
    if (loaded.status == ProjectResumeLoadStatus::Missing) {
        showHome();
        return {ProjectResumeStatus::NoBookmark, false, false};
    }
    if (loaded.status != ProjectResumeLoadStatus::Loaded || !loaded.bookmark ||
        !loaded.bookmark->valid()) {
        const bool cleared = m_store.clear();
        showHome();
        return {cleared ? ProjectResumeStatus::InvalidBookmark
                        : ProjectResumeStatus::PersistenceFailure,
                false,
                false,
                cleared};
    }
    const auto& bookmark = *loaded.bookmark;

    if (!m_callbacks.inspectProject ||
        m_callbacks.inspectProject(bookmark) != ResumeProjectStatus::Ready) {
        const bool cleared = m_store.clear();
        showHome();
        return {cleared ? ProjectResumeStatus::InvalidBookmark
                        : ProjectResumeStatus::PersistenceFailure,
                false,
                false,
                cleared};
    }

    if (!m_callbacks.activateProject) {
        showHome();
        return {ProjectResumeStatus::ActivationRejected, false, false};
    }
    const auto projectActivation = m_callbacks.activateProject(bookmark.project);
    if (projectActivation == ResumeActivationStatus::Superseded) {
        return {ProjectResumeStatus::ActivationSuperseded, false, false};
    }
    if (projectActivation != ResumeActivationStatus::Applied) {
        const bool cleared = m_store.clear();
        showHome();
        return {cleared ? ProjectResumeStatus::ActivationRejected
                        : ProjectResumeStatus::PersistenceFailure,
                false,
                false,
                cleared};
    }

    if (!bookmark.item) {
        return {ProjectResumeStatus::ProjectRestored, true, false};
    }

    const ProjectItemRef item{bookmark.project, *bookmark.item};
    if (!m_callbacks.inspectItem ||
        m_callbacks.inspectItem(item) != ResumeItemStatus::Ready ||
        !m_callbacks.activateItem) {
        const bool cleared = clearItem();
        return {ProjectResumeStatus::ProjectRestored, true, false, cleared};
    }

    const auto itemActivation = m_callbacks.activateItem(item);
    if (itemActivation == ResumeActivationStatus::Pending) {
        return {ProjectResumeStatus::ItemActivationPending, true, false};
    }
    if (itemActivation != ResumeActivationStatus::Applied) {
        const bool cleared = clearItem();
        return {ProjectResumeStatus::ProjectRestored, true, false, cleared};
    }
    return {ProjectResumeStatus::ProjectAndItemRestored, true, true};
}

bool ProjectResumeCoordinator::rememberProject(ProjectId project) {
    if (!project.valid()) {
        return m_store.clear();
    }
    return m_store.save(ProjectResumeBookmark{project, std::nullopt});
}

bool ProjectResumeCoordinator::rememberItem(ProjectItemRef item) {
    if (!item.valid())
        return false;
    auto loaded = m_store.load();
    if (loaded.status != ProjectResumeLoadStatus::Loaded || !loaded.bookmark ||
        loaded.bookmark->project != item.project) {
        return false;
    }
    loaded.bookmark->item = item.item;
    return m_store.save(*loaded.bookmark);
}

bool ProjectResumeCoordinator::clearItem() {
    auto loaded = m_store.load();
    if (loaded.status == ProjectResumeLoadStatus::Missing)
        return true;
    if (loaded.status != ProjectResumeLoadStatus::Loaded || !loaded.bookmark)
        return false;
    if (!loaded.bookmark->item)
        return true;
    loaded.bookmark->item.reset();
    return m_store.save(*loaded.bookmark);
}

bool ProjectResumeCoordinator::completeClose(ProjectClosePurpose purpose) {
    if (purpose == ProjectClosePurpose::ExplicitClose)
        return m_store.clear();
    return true;
}

bool ProjectResumeCoordinator::completeDestruction() {
    return m_store.clear();
}

void ProjectResumeCoordinator::showHome() const {
    if (m_callbacks.showHome)
        m_callbacks.showHome();
}

} // namespace dw::workshop
