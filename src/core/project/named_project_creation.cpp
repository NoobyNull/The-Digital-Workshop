#include "named_project_creation.h"

#include <utility>

#include "project.h"

namespace dw {

NamedProjectCreationService::NamedProjectCreationService(ProjectManager& projectManager)
    : m_projectManager(projectManager) {}

NamedProjectPrepareResult NamedProjectCreationService::prepare(std::string name) {
    auto project = m_projectManager.create(name, true);
    if (!project)
        return {NamedProjectPrepareStatus::CreateFailed, {}, nullptr};

    const i64 projectId = project->id();
    if (!m_projectManager.save(*project)) {
        const bool cleaned = m_projectManager.remove(projectId);
        return {cleaned ? NamedProjectPrepareStatus::StorageFailed
                        : NamedProjectPrepareStatus::CleanupFailed,
                NamedProjectCreationToken{projectId, project->filePath()},
                cleaned ? nullptr : std::move(project)};
    }

    NamedProjectCreationToken token{projectId, project->filePath()};
    return {NamedProjectPrepareStatus::Prepared, std::move(token), std::move(project)};
}

NamedProjectFinishStatus NamedProjectCreationService::finish(
    const NamedProjectCreationToken& token, bool activationAccepted) {
    if (!token.valid())
        return NamedProjectFinishStatus::InvalidToken;

    const auto active = m_projectManager.currentProject();
    const bool tokenIsActive = active && active->id() == token.projectId;

    if (!activationAccepted) {
        // A contradictory callback must never delete the project currently
        // synchronized as active. Preserve it for recovery instead.
        if (tokenIsActive)
            return NamedProjectFinishStatus::ActiveIdentityMismatch;
        return m_projectManager.discardTemporaryProjectData(token.projectId, token.root)
                   ? NamedProjectFinishStatus::RejectedCleaned
                   : NamedProjectFinishStatus::CleanupFailed;
    }

    if (!tokenIsActive || !active->isTemporary() || active->filePath() != token.root)
        return NamedProjectFinishStatus::ActiveIdentityMismatch;

    if (m_projectManager.saveTemporaryProject())
        return NamedProjectFinishStatus::Published;

    active->markModified();
    return NamedProjectFinishStatus::NeedsSaving;
}

} // namespace dw
