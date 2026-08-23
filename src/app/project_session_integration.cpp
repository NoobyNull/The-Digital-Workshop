#include "app/project_session_integration.h"

#include <utility>

#include "core/project/project.h"

namespace dw {
namespace {

workshop::WorkshopCommand command(workshop::WorkshopCommandPayload payload) {
    return {std::move(payload), std::nullopt};
}

std::optional<workshop::ProjectId> projectId(const std::shared_ptr<Project>& project) {
    if (!project || project->id() <= 0)
        return std::nullopt;
    return workshop::ProjectId(project->id());
}

} // namespace

ProjectSessionIntegration::ProjectSessionIntegration(workshop::ProjectSession& session,
                                                     ProjectManager& projectManager,
                                                     CommitCallback onCommit,
                                                     SaveCallback saveProject,
                                                     SavePreparationCallback savePreparation)
    : m_session(session), m_projectManager(projectManager), m_onCommit(std::move(onCommit)),
      m_saveProject(std::move(saveProject)),
      m_savePreparation(std::move(savePreparation)) {
    if (!m_saveProject) {
        m_saveProject = [this](Project& project) {
            return m_projectManager.save(project);
        };
    }
}

ProjectSessionIntegrationResult ProjectSessionIntegration::activateProject(
    std::shared_ptr<Project> project,
    std::optional<workshop::ContextGeneration> expectedGeneration) {
    if (expectedGeneration.has_value() && *expectedGeneration != m_session.snapshot().generation) {
        return {
            workshop::WorkshopTransition{workshop::TransitionStatus::Rejected,
                                         workshop::TransitionReason::StaleGeneration,
                                         m_session.snapshot()},
            ProjectSessionIntegrationError::None,
        };
    }
    if (auto dirtyTransition = synchronizeOutgoingDirty();
        dirtyTransition.has_value() && !dirtyTransition->accepted()) {
        return {*dirtyTransition, ProjectSessionIntegrationError::None};
    }
    return requestActivation(std::move(project));
}

ProjectSessionIntegrationResult ProjectSessionIntegration::closeProject() {
    if (auto dirtyTransition = synchronizeOutgoingDirty();
        dirtyTransition.has_value() && !dirtyTransition->accepted()) {
        return {*dirtyTransition, ProjectSessionIntegrationError::None};
    }
    return requestClose();
}

ProjectSessionIntegrationResult ProjectSessionIntegration::resolvePending(
    workshop::ConfirmationToken token, ProjectTransitionChoice choice) {
    if (!m_pending.has_value() || !token.valid() || token != m_pending->token)
        return staleConfirmation();

    if (!m_session.hasPendingTransition()) {
        m_pending.reset();
        return staleConfirmation();
    }

    if (choice == ProjectTransitionChoice::Cancel) {
        const auto transition = m_session.dispatch(command(workshop::ResolvePendingTransition{
            token, workshop::PendingTransitionResolution::Cancel}));
        if (transition.status == workshop::TransitionStatus::Unchanged)
            m_pending.reset();
        return {transition, ProjectSessionIntegrationError::None};
    }

    PendingCommit& pending = *m_pending;
    if (choice == ProjectTransitionChoice::Save &&
        pending.confirmation.pendingChanges.unsavedPreparation &&
        (!m_savePreparation || !m_savePreparation())) {
        auto confirmation = pending.confirmation;
        confirmation.context = m_session.snapshot();
        return {confirmation, ProjectSessionIntegrationError::SaveFailed};
    }
    if (choice == ProjectTransitionChoice::Save &&
        pending.confirmation.pendingChanges.unsavedProject) {
        if (!pending.outgoing || !m_saveProject(*pending.outgoing)) {
            auto confirmation = pending.confirmation;
            confirmation.context = m_session.snapshot();
            return {confirmation, ProjectSessionIntegrationError::SaveFailed};
        }
    }

    const auto previousProject = projectId(pending.outgoing);
    auto replacement = pending.replacement;
    auto transition = m_session.dispatch(command(workshop::ResolvePendingTransition{
        token, workshop::PendingTransitionResolution::ChangesResolved}));

    if (transition.status == workshop::TransitionStatus::Applied) {
        m_pending.reset();
        commitApplied(std::move(replacement), previousProject, transition);
    } else if (!m_session.hasPendingTransition()) {
        m_pending.reset();
    }

    return {transition, ProjectSessionIntegrationError::None};
}

bool ProjectSessionIntegration::hasPendingCommit() const noexcept {
    return m_pending.has_value();
}

ProjectSessionIntegrationResult ProjectSessionIntegration::requestActivation(
    std::shared_ptr<Project> project) {
    const auto candidateId = projectId(project);
    const workshop::ProjectId id = candidateId.value_or(workshop::ProjectId{});
    const auto previousProject = projectId(m_projectManager.currentProject());
    auto transition = m_session.dispatch(command(workshop::ActivateProject{id}));

    if (transition.status == workshop::TransitionStatus::ConfirmationRequired) {
        rememberPending(transition, m_projectManager.currentProject(), std::move(project));
    } else if (transition.status == workshop::TransitionStatus::Applied) {
        if (project && project->isModified()) {
            const auto dirty = m_session.dispatch(command(workshop::SetProjectDirty{true}));
            if (!dirty.accepted())
                return {dirty, ProjectSessionIntegrationError::None};
            transition.context = m_session.snapshot();
        }
        commitApplied(std::move(project), previousProject, transition);
    }

    return {transition, ProjectSessionIntegrationError::None};
}

ProjectSessionIntegrationResult ProjectSessionIntegration::requestClose() {
    const auto outgoing = m_projectManager.currentProject();
    const auto previousProject = projectId(outgoing);
    auto transition = m_session.dispatch(command(workshop::CloseProject{}));

    if (transition.status == workshop::TransitionStatus::ConfirmationRequired) {
        rememberPending(transition, outgoing, nullptr);
    } else if (transition.status == workshop::TransitionStatus::Applied) {
        commitApplied(nullptr, previousProject, transition);
    }

    return {transition, ProjectSessionIntegrationError::None};
}

std::optional<workshop::WorkshopTransition> ProjectSessionIntegration::synchronizeOutgoingDirty() {
    const auto current = m_projectManager.currentProject();
    const auto active = m_session.snapshot().activeProject;
    const auto currentId = projectId(current);
    if (!active.has_value() || !currentId.has_value() || *active != *currentId)
        return std::nullopt;

    return m_session.dispatch(command(workshop::SetProjectDirty{current->isModified()}));
}

void ProjectSessionIntegration::rememberPending(const workshop::WorkshopTransition& transition,
                                                std::shared_ptr<Project> outgoing,
                                                std::shared_ptr<Project> replacement) {
    if (!transition.confirmation.has_value())
        return;

    m_pending = PendingCommit{
        *transition.confirmation, std::move(outgoing), std::move(replacement), transition};
}

void ProjectSessionIntegration::commitApplied(std::shared_ptr<Project> replacement,
                                              std::optional<workshop::ProjectId> previousProject,
                                              workshop::WorkshopTransition& transition) {
    m_projectManager.synchronizeActiveProject(replacement);

    if (replacement && replacement->isModified()) {
        const auto dirty = m_session.dispatch(command(workshop::SetProjectDirty{true}));
        if (dirty.accepted())
            transition.context = m_session.snapshot();
    }

    if (m_onCommit) {
        m_onCommit(ProjectSessionCommit{
            previousProject, projectId(replacement), transition.context.generation});
    }
}

ProjectSessionIntegrationResult ProjectSessionIntegration::staleConfirmation() const {
    return {
        workshop::WorkshopTransition{workshop::TransitionStatus::Rejected,
                                     workshop::TransitionReason::StaleConfirmation,
                                     m_session.snapshot()},
        ProjectSessionIntegrationError::None,
    };
}

} // namespace dw
