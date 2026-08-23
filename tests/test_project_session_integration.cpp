#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "app/project_session_integration.h"
#include "core/database/database.h"
#include "core/project/project.h"

namespace dw {
namespace {

class ProjectSessionIntegrationTest : public ::testing::Test {
  protected:
    void SetUp() override { rebuildIntegration(); }

    std::shared_ptr<Project> project(i64 id, bool modified = false) {
        auto value = std::make_shared<Project>();
        value->record().id = id;
        value->record().name = "Project " + std::to_string(id);
        if (modified)
            value->markModified();
        return value;
    }

    void rebuildIntegration() {
        m_integration = std::make_unique<ProjectSessionIntegration>(
            m_session,
            m_manager,
            [this](const ProjectSessionCommit& commit) { m_commits.push_back(commit); },
            [this](Project& value) {
                ++m_saveCalls;
                if (!m_saveSucceeds)
                    return false;
                value.clearModified();
                return true;
            },
            [this]() {
                ++m_preparationSaveCalls;
                return m_preparationSaveSucceeds;
            });
    }

    workshop::WorkshopTransition send(workshop::WorkshopCommandPayload payload) {
        return m_session.dispatch(workshop::WorkshopCommand{std::move(payload), std::nullopt});
    }

    Database m_database;
    workshop::ProjectSession m_session;
    ProjectManager m_manager{m_database};
    std::unique_ptr<ProjectSessionIntegration> m_integration;
    std::vector<ProjectSessionCommit> m_commits;
    int m_saveCalls = 0;
    bool m_saveSucceeds = true;
    int m_preparationSaveCalls = 0;
    bool m_preparationSaveSucceeds = true;
};

} // namespace

TEST_F(ProjectSessionIntegrationTest, InitialActivationCommitsManagerAndFocusOnce) {
    auto first = project(1, true);

    const auto result = m_integration->activateProject(first);

    EXPECT_EQ(result.transition.status, workshop::TransitionStatus::Applied);
    EXPECT_EQ(result.transition.context.activeProject, workshop::ProjectId(1));
    EXPECT_TRUE(result.transition.context.projectDirty);
    EXPECT_EQ(m_manager.currentProject(), first);
    ASSERT_EQ(m_commits.size(), 1U);
    EXPECT_FALSE(m_commits[0].previousProject.has_value());
    EXPECT_EQ(m_commits[0].activeProject, workshop::ProjectId(1));
    EXPECT_EQ(m_commits[0].generation, result.transition.context.generation);
}

TEST_F(ProjectSessionIntegrationTest, SameFocusedProjectIsIdempotent) {
    auto first = project(1);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    m_commits.clear();

    const auto result = m_integration->activateProject(first);

    EXPECT_EQ(result.transition.status, workshop::TransitionStatus::Unchanged);
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_TRUE(m_commits.empty());
}

TEST_F(ProjectSessionIntegrationTest, StaleExpectedGenerationCannotReplaceProject) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    const auto staleGeneration = workshop::ContextGeneration{0};
    m_commits.clear();

    const auto result = m_integration->activateProject(second, staleGeneration);

    EXPECT_EQ(result.transition.status, workshop::TransitionStatus::Rejected);
    EXPECT_EQ(result.transition.reason, workshop::TransitionReason::StaleGeneration);
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_EQ(m_session.snapshot().activeProject, workshop::ProjectId(1));
    EXPECT_TRUE(m_commits.empty());
}

TEST_F(ProjectSessionIntegrationTest, DirtySwitchConfirmationCanCancelByToken) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    first->markModified();
    m_commits.clear();

    const auto requested = m_integration->activateProject(second);
    ASSERT_EQ(requested.transition.status, workshop::TransitionStatus::ConfirmationRequired);
    ASSERT_TRUE(requested.transition.confirmation.has_value());
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_TRUE(m_integration->hasPendingCommit());

    const auto stale = m_integration->resolvePending(
        workshop::ConfirmationToken{requested.transition.confirmation->value + 1},
        ProjectTransitionChoice::Cancel);
    EXPECT_EQ(stale.transition.reason, workshop::TransitionReason::StaleConfirmation);
    EXPECT_TRUE(m_integration->hasPendingCommit());

    const auto cancelled = m_integration->resolvePending(*requested.transition.confirmation,
                                                         ProjectTransitionChoice::Cancel);
    EXPECT_EQ(cancelled.transition.status, workshop::TransitionStatus::Unchanged);
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_EQ(m_session.snapshot().activeProject, workshop::ProjectId(1));
    EXPECT_TRUE(m_commits.empty());
    EXPECT_FALSE(m_integration->hasPendingCommit());
}

TEST_F(ProjectSessionIntegrationTest, SaveResolutionPersistsBeforeFinalCommit) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    first->markModified();
    m_commits.clear();

    const auto requested = m_integration->activateProject(second);
    ASSERT_TRUE(requested.transition.confirmation.has_value());

    const auto resolved = m_integration->resolvePending(*requested.transition.confirmation,
                                                        ProjectTransitionChoice::Save);

    EXPECT_TRUE(resolved.committed());
    EXPECT_EQ(resolved.error, ProjectSessionIntegrationError::None);
    EXPECT_EQ(m_saveCalls, 1);
    EXPECT_FALSE(first->isModified());
    EXPECT_EQ(m_manager.currentProject(), second);
    EXPECT_EQ(m_session.snapshot().activeProject, workshop::ProjectId(2));
    ASSERT_EQ(m_commits.size(), 1U);
    EXPECT_EQ(m_commits[0].previousProject, workshop::ProjectId(1));
    EXPECT_EQ(m_commits[0].activeProject, workshop::ProjectId(2));
}

TEST_F(ProjectSessionIntegrationTest, DiscardResolutionSkipsSaveAndCommitsReplacement) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    first->markModified();
    m_commits.clear();

    const auto requested = m_integration->activateProject(second);
    ASSERT_TRUE(requested.transition.confirmation.has_value());
    const auto resolved = m_integration->resolvePending(*requested.transition.confirmation,
                                                        ProjectTransitionChoice::Discard);

    EXPECT_TRUE(resolved.committed());
    EXPECT_EQ(m_saveCalls, 0);
    EXPECT_TRUE(first->isModified());
    EXPECT_EQ(m_manager.currentProject(), second);
    ASSERT_EQ(m_commits.size(), 1U);
}

TEST_F(ProjectSessionIntegrationTest, CloseCommitsNullManagerProject) {
    auto first = project(1);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    m_commits.clear();

    const auto result = m_integration->closeProject();

    EXPECT_TRUE(result.committed());
    EXPECT_FALSE(m_manager.currentProject());
    EXPECT_FALSE(m_session.snapshot().activeProject.has_value());
    EXPECT_EQ(m_session.snapshot().route, workshop::WorkshopRoute::Home);
    ASSERT_EQ(m_commits.size(), 1U);
    EXPECT_EQ(m_commits[0].previousProject, workshop::ProjectId(1));
    EXPECT_FALSE(m_commits[0].activeProject.has_value());
}

TEST_F(ProjectSessionIntegrationTest, ActiveRunBlocksReplacementAndClose) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    ASSERT_TRUE(send(workshop::BeginRun{workshop::RunLockRef{workshop::RunId(9), std::nullopt}})
                    .accepted());
    m_commits.clear();

    const auto replacement = m_integration->activateProject(second);
    const auto close = m_integration->closeProject();

    EXPECT_EQ(replacement.transition.status, workshop::TransitionStatus::Blocked);
    EXPECT_EQ(replacement.transition.reason, workshop::TransitionReason::ActiveRun);
    EXPECT_EQ(close.transition.status, workshop::TransitionStatus::Blocked);
    EXPECT_EQ(close.transition.reason, workshop::TransitionReason::ActiveRun);
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_TRUE(m_commits.empty());
}

TEST_F(ProjectSessionIntegrationTest, SaveFailureKeepsPendingAndManagerUntouched) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    first->markModified();
    m_saveSucceeds = false;
    m_commits.clear();

    const auto requested = m_integration->activateProject(second);
    ASSERT_TRUE(requested.transition.confirmation.has_value());
    const auto failed = m_integration->resolvePending(*requested.transition.confirmation,
                                                      ProjectTransitionChoice::Save);

    EXPECT_EQ(failed.error, ProjectSessionIntegrationError::SaveFailed);
    EXPECT_EQ(failed.transition.status, workshop::TransitionStatus::ConfirmationRequired);
    EXPECT_EQ(m_saveCalls, 1);
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_EQ(m_session.snapshot().activeProject, workshop::ProjectId(1));
    EXPECT_TRUE(m_session.hasPendingTransition());
    EXPECT_TRUE(m_integration->hasPendingCommit());
    EXPECT_TRUE(m_commits.empty());
}

TEST_F(ProjectSessionIntegrationTest, PreparationSaveRunsBeforeProjectSwitchCommit) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    ASSERT_TRUE(send(workshop::SelectProjectItem{
                         {workshop::ProjectId(1), workshop::ProjectItemId(10)}})
                    .accepted());
    ASSERT_TRUE(send(workshop::SetPreparationLock{true}).accepted());

    const auto requested = m_integration->activateProject(second);
    ASSERT_TRUE(requested.transition.confirmation.has_value());
    const auto resolved = m_integration->resolvePending(
        *requested.transition.confirmation, ProjectTransitionChoice::Save);

    EXPECT_TRUE(resolved.committed());
    EXPECT_EQ(m_preparationSaveCalls, 1);
    EXPECT_EQ(m_manager.currentProject(), second);
    EXPECT_FALSE(m_session.snapshot().preparationLocked);
}

TEST_F(ProjectSessionIntegrationTest, PreparationSaveFailureKeepsExactProjectPending) {
    auto first = project(1);
    auto second = project(2);
    ASSERT_TRUE(m_integration->activateProject(first).committed());
    ASSERT_TRUE(send(workshop::SelectProjectItem{
                         {workshop::ProjectId(1), workshop::ProjectItemId(10)}})
                    .accepted());
    ASSERT_TRUE(send(workshop::SetPreparationLock{true}).accepted());
    m_preparationSaveSucceeds = false;

    const auto requested = m_integration->activateProject(second);
    ASSERT_TRUE(requested.transition.confirmation.has_value());
    const auto failed = m_integration->resolvePending(
        *requested.transition.confirmation, ProjectTransitionChoice::Save);

    EXPECT_EQ(failed.error, ProjectSessionIntegrationError::SaveFailed);
    EXPECT_EQ(m_preparationSaveCalls, 1);
    EXPECT_EQ(m_manager.currentProject(), first);
    EXPECT_EQ(m_session.snapshot().activeProject, workshop::ProjectId(1));
    EXPECT_TRUE(m_session.snapshot().preparationLocked);
    EXPECT_TRUE(m_integration->hasPendingCommit());
}

} // namespace dw
