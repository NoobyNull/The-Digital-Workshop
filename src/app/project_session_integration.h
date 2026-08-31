#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "modules/project_session/project_session.h"

namespace dw {

class Project;
class ProjectManager;

enum class ProjectTransitionChoice {
    Save,
    Discard,
    Cancel,
};

enum class ProjectSessionIntegrationError {
    None,
    SaveFailed,
};

struct ProjectSessionCommit {
    std::optional<workshop::ProjectId> previousProject;
    std::optional<workshop::ProjectId> activeProject;
    workshop::ContextGeneration generation;
};

struct ProjectSessionIntegrationResult {
    workshop::WorkshopTransition transition;
    ProjectSessionIntegrationError error = ProjectSessionIntegrationError::None;

    [[nodiscard]] bool committed() const noexcept {
        return transition.status == workshop::TransitionStatus::Applied &&
               error == ProjectSessionIntegrationError::None;
    }
};

class ProjectSessionIntegration {
  public:
    using CommitCallback = std::function<void(const ProjectSessionCommit&)>;
    using SaveCallback = std::function<bool(Project&)>;
    using SavePreparationCallback = std::function<bool()>;

    ProjectSessionIntegration(workshop::ProjectSession& session,
                              ProjectManager& projectManager,
                              CommitCallback onCommit = {},
                              SaveCallback saveProject = {},
                              SavePreparationCallback savePreparation = {});
    ProjectSessionIntegration(const ProjectSessionIntegration&) = delete;
    ProjectSessionIntegration& operator=(const ProjectSessionIntegration&) = delete;
    ProjectSessionIntegration(ProjectSessionIntegration&&) = delete;
    ProjectSessionIntegration& operator=(ProjectSessionIntegration&&) = delete;

    ProjectSessionIntegrationResult activateProject(
        std::shared_ptr<Project> project,
        std::optional<workshop::ContextGeneration> expectedGeneration = std::nullopt);
    ProjectSessionIntegrationResult closeProject();
    ProjectSessionIntegrationResult resolvePending(workshop::ConfirmationToken token,
                                                   ProjectTransitionChoice choice);

    [[nodiscard]] bool hasPendingCommit() const noexcept;

  private:
    struct PendingCommit {
        workshop::ConfirmationToken token;
        std::shared_ptr<Project> outgoing;
        std::shared_ptr<Project> replacement;
        workshop::WorkshopTransition confirmation;
    };

    ProjectSessionIntegrationResult requestActivation(std::shared_ptr<Project> project);
    ProjectSessionIntegrationResult requestClose();
    std::optional<workshop::WorkshopTransition> synchronizeOutgoingDirty();
    void rememberPending(const workshop::WorkshopTransition& transition,
                         std::shared_ptr<Project> outgoing,
                         std::shared_ptr<Project> replacement);
    void commitApplied(std::shared_ptr<Project> replacement,
                       std::optional<workshop::ProjectId> previousProject,
                       workshop::WorkshopTransition& transition);
    [[nodiscard]] ProjectSessionIntegrationResult staleConfirmation() const;

    workshop::ProjectSession& m_session;
    ProjectManager& m_projectManager;
    CommitCallback m_onCommit;
    SaveCallback m_saveProject;
    SavePreparationCallback m_savePreparation;
    std::optional<PendingCommit> m_pending;
};

} // namespace dw
