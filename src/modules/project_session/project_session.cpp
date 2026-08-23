#include "project_session.h"

#include <type_traits>
#include <utility>

namespace dw::workshop {

WorkshopContextSnapshot ProjectSession::snapshot() const {
    return m_context;
}

bool ProjectSession::hasPendingTransition() const noexcept {
    return m_pendingCommand.has_value();
}

WorkshopTransition ProjectSession::dispatch(const WorkshopCommand& command) {
    if (command.expectedGeneration.has_value() &&
        *command.expectedGeneration != m_context.generation) {
        return rejected(TransitionStatus::Rejected, TransitionReason::StaleGeneration);
    }

    if (std::holds_alternative<ResolvePendingTransition>(command.payload))
        return dispatchInternal(command);

    if (m_pendingCommand.has_value()) {
        const auto* beginRun = std::get_if<BeginRun>(&command.payload);
        if (beginRun == nullptr) {
            return rejected(TransitionStatus::Blocked,
                            TransitionReason::PendingConfirmation);
        }
        WorkshopTransition transition = dispatchInternal(command);
        if (transition.status == TransitionStatus::Applied) {
            m_pendingCommand.reset();
            m_pendingConfirmation.reset();
            m_pendingChanges = {};
        }
        return transition;
    }

    return dispatchInternal(command);
}

WorkshopTransition ProjectSession::dispatchInternal(const WorkshopCommand& command) {
    return std::visit(
        [this, &command](const auto& payload) {
            return handle(command, payload);
        },
        command.payload);
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& command,
                                          const ActivateProject& activate) {
    if (!activate.project.valid())
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidReference);

    const bool sameProject =
        m_context.activeProject.has_value() && *m_context.activeProject == activate.project;
    const bool alreadyFocused = sameProject && m_context.route == WorkshopRoute::Project &&
                                !m_context.libraryPreview.has_value();
    if (alreadyFocused)
        return unchanged();
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);

    if (!sameProject) {
        const PendingChanges pendingChanges = unresolvedProjectChange();
        if (pendingChanges.any())
            return requestConfirmation(command, pendingChanges);
        m_context.activeProjectItem.reset();
        m_context.projectDirty = false;
        m_context.preparationLocked = false;
    }

    m_context.activeProject = activate.project;
    m_context.route = WorkshopRoute::Project;
    clearPreviewAndReturn();
    restoreProjectOrigin();
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& command,
                                          const CloseProject& /*close*/) {
    if (!m_context.activeProject.has_value())
        return unchanged();
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);
    const PendingChanges pendingChanges = unresolvedProjectChange();
    if (pendingChanges.any())
        return requestConfirmation(command, pendingChanges);

    const ContextGeneration generation = m_context.generation;
    m_context = WorkshopContextSnapshot{};
    m_context.generation = generation;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& command,
                                          const SelectProjectItem& select) {
    if (!select.item.valid())
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidReference);
    if (!m_context.activeProject.has_value())
        return rejected(TransitionStatus::Rejected, TransitionReason::NoActiveProject);
    if (select.item.project != *m_context.activeProject)
        return rejected(TransitionStatus::Rejected, TransitionReason::ProjectMismatch);

    const bool sameItem = m_context.activeProjectItem.has_value() &&
                          m_context.activeProjectItem->project == select.item.project &&
                          m_context.activeProjectItem->item == select.item.item;
    const bool alreadyFocused = sameItem && m_context.route == WorkshopRoute::Project &&
                                !m_context.libraryPreview.has_value();
    if (alreadyFocused)
        return unchanged();
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);
    if (m_context.preparationLocked && !sameItem)
        return requestConfirmation(command, PendingChanges{false, true});

    m_context.activeProjectItem = select.item;
    m_context.route = WorkshopRoute::Project;
    clearPreviewAndReturn();
    m_context.origin = SelectionOrigin::ProjectItem;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& command,
                                          const ClearProjectItem& /*clear*/) {
    const bool hasSelection = m_context.activeProjectItem.has_value() ||
                              m_context.libraryPreview.has_value();
    if (!hasSelection)
        return unchanged();
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);
    if (m_context.preparationLocked)
        return requestConfirmation(command, PendingChanges{false, true});

    m_context.activeProjectItem.reset();
    clearPreviewAndReturn();
    if (m_context.route == WorkshopRoute::DesignLibrary)
        m_context.route = m_context.activeProject.has_value()
            ? WorkshopRoute::Project
            : WorkshopRoute::Home;
    m_context.origin = SelectionOrigin::None;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const NavigateTo& navigate) {
    const bool alreadyThere = m_context.route == navigate.route;
    if (alreadyThere)
        return unchanged();
    if (navigate.route == WorkshopRoute::RunCnc)
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidTransition);
    if (m_context.runLocked() && navigate.route != WorkshopRoute::RunCnc)
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);
    if (navigate.route == WorkshopRoute::Project &&
        !m_context.activeProject.has_value()) {
        return rejected(TransitionStatus::Rejected, TransitionReason::NoActiveProject);
    }

    if (navigate.route == WorkshopRoute::DesignLibrary) {
        m_context.libraryReturnRoute = m_context.route;
        m_context.libraryPreview.reset();
    } else {
        clearPreviewAndReturn();
    }
    m_context.route = navigate.route;
    restoreProjectOrigin();
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const PreviewLibraryItem& preview) {
    if (!preview.item.valid())
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidReference);
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);
    if (m_context.route != WorkshopRoute::DesignLibrary)
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidTransition);

    const bool samePreview = m_context.libraryPreview.has_value() &&
                             m_context.libraryPreview->kind == preview.item.kind &&
                             m_context.libraryPreview->item == preview.item.item;
    if (samePreview && m_context.route == WorkshopRoute::DesignLibrary)
        return unchanged();
    m_context.libraryPreview = preview.item;
    m_context.origin = SelectionOrigin::LibraryPreview;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const ReturnFromLibrary& /*returnFromLibrary*/) {
    const bool hasLibraryState = m_context.route == WorkshopRoute::DesignLibrary ||
                                 m_context.libraryPreview.has_value() ||
                                 m_context.libraryReturnRoute.has_value();
    if (!hasLibraryState)
        return unchanged();
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);

    WorkshopRoute destination = m_context.libraryReturnRoute.value_or(
        m_context.activeProject.has_value() ? WorkshopRoute::Project
                                            : WorkshopRoute::Home);
    if (destination == WorkshopRoute::Project && !m_context.activeProject.has_value())
        destination = WorkshopRoute::Home;

    clearPreviewAndReturn();
    m_context.route = destination;
    restoreProjectOrigin();
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const SetProjectDirty& setDirty) {
    if (setDirty.dirty && !m_context.activeProject.has_value())
        return rejected(TransitionStatus::Rejected, TransitionReason::NoActiveProject);
    if (m_context.projectDirty == setDirty.dirty)
        return unchanged();

    m_context.projectDirty = setDirty.dirty;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const SetPreparationLock& setLock) {
    if (setLock.locked && (!m_context.activeProject.has_value() ||
                           !m_context.activeProjectItem.has_value())) {
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidTransition);
    }
    if (m_context.preparationLocked == setLock.locked)
        return unchanged();
    if (m_context.runLocked())
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);

    m_context.preparationLocked = setLock.locked;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const BeginRun& beginRun) {
    if (!beginRun.run.valid())
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidReference);
    if (m_context.activeRun.has_value()) {
        if (m_context.activeRun->run == beginRun.run.run)
            return unchanged();
        return rejected(TransitionStatus::Blocked, TransitionReason::ActiveRun);
    }

    if (beginRun.run.projectPinned()) {
        const ProjectItemRef& operation = *beginRun.run.projectOperation;
        if (!m_context.activeProject.has_value())
            return rejected(TransitionStatus::Rejected, TransitionReason::NoActiveProject);
        if (operation.project != *m_context.activeProject ||
            !m_context.activeProjectItem.has_value() ||
            m_context.activeProjectItem->project != operation.project ||
            m_context.activeProjectItem->item != operation.item) {
            return rejected(TransitionStatus::Rejected, TransitionReason::ProjectMismatch);
        }
    } else if (m_context.preparationLocked) {
        return rejected(TransitionStatus::Blocked,
                        TransitionReason::UnsavedPreparation);
    }

    m_context.runReturnRoute = m_context.route == WorkshopRoute::RunCnc
        ? std::optional<WorkshopRoute>{}
        : std::optional<WorkshopRoute>{m_context.route};
    m_context.activeRun = beginRun.run;
    m_context.preparationLocked = false;
    m_context.route = WorkshopRoute::RunCnc;
    clearPreviewAndReturn();
    m_context.origin = beginRun.run.projectPinned()
        ? SelectionOrigin::ProjectItem
        : SelectionOrigin::ExternalRun;
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const EndRun& endRun) {
    if (!endRun.run.valid())
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidReference);
    if (!m_context.activeRun.has_value())
        return unchanged();
    if (m_context.activeRun->run != endRun.run)
        return rejected(TransitionStatus::Rejected, TransitionReason::RunMismatch);

    m_context.activeRun.reset();
    WorkshopRoute destination = m_context.runReturnRoute.value_or(
        m_context.activeProject.has_value() ? WorkshopRoute::Project
                                            : WorkshopRoute::Home);
    if (destination == WorkshopRoute::DesignLibrary) {
        destination = m_context.activeProject.has_value()
            ? WorkshopRoute::Project
            : WorkshopRoute::Home;
    }
    m_context.runReturnRoute.reset();
    m_context.route = destination;
    restoreProjectOrigin();
    return applied();
}

WorkshopTransition ProjectSession::handle(const WorkshopCommand& /*command*/,
                                          const ResolvePendingTransition& resolve) {
    if (!m_pendingCommand.has_value())
        return unchanged();

    if (!resolve.token.valid() || !m_pendingConfirmation.has_value() ||
        resolve.token != *m_pendingConfirmation) {
        return rejected(TransitionStatus::Rejected,
                        TransitionReason::StaleConfirmation);
    }

    if (resolve.resolution == PendingTransitionResolution::Cancel) {
        m_pendingCommand.reset();
        m_pendingConfirmation.reset();
        m_pendingChanges = {};
        return unchanged();
    }

    WorkshopCommand pending = std::move(*m_pendingCommand);
    const PendingChanges resolvedChanges = m_pendingChanges;
    m_pendingCommand.reset();
    m_pendingConfirmation.reset();
    m_pendingChanges = {};

    if (resolvedChanges.unsavedPreparation)
        m_context.preparationLocked = false;
    if (resolvedChanges.unsavedProject)
        m_context.projectDirty = false;
    if (!resolvedChanges.any())
        return rejected(TransitionStatus::Rejected, TransitionReason::InvalidTransition);
    return dispatchInternal(pending);
}

WorkshopTransition ProjectSession::applied() {
    ++m_context.generation.value;
    return {
        TransitionStatus::Applied,
        TransitionReason::None,
        m_context,
    };
}

WorkshopTransition ProjectSession::unchanged() const {
    return {
        TransitionStatus::Unchanged,
        TransitionReason::None,
        m_context,
    };
}

WorkshopTransition ProjectSession::rejected(TransitionStatus status,
                                            TransitionReason reason) const {
    return {status, reason, m_context};
}

WorkshopTransition ProjectSession::requestConfirmation(
    const WorkshopCommand& command, PendingChanges pendingChanges) {
    m_pendingCommand = command;
    m_pendingConfirmation = ConfirmationToken{m_nextConfirmationValue++};
    m_pendingChanges = pendingChanges;
    const TransitionReason reason = pendingChanges.unsavedPreparation
        ? TransitionReason::UnsavedPreparation
        : TransitionReason::UnsavedProject;
    WorkshopTransition transition =
        rejected(TransitionStatus::ConfirmationRequired, reason);
    transition.confirmation = m_pendingConfirmation;
    transition.pendingChanges = pendingChanges;
    return transition;
}

PendingChanges ProjectSession::unresolvedProjectChange() const noexcept {
    return {m_context.projectDirty, m_context.preparationLocked};
}

void ProjectSession::clearPreviewAndReturn() {
    m_context.libraryPreview.reset();
    m_context.libraryReturnRoute.reset();
}

void ProjectSession::restoreProjectOrigin() {
    m_context.origin = m_context.activeProjectItem.has_value()
        ? SelectionOrigin::ProjectItem
        : SelectionOrigin::None;
}

} // namespace dw::workshop
