#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "app/project_plan_run_truth_adapter.h"

namespace {

using namespace dw;

constexpr workshop::ProjectId kProject(9);

workshop::ProjectItemRef ref(i64 item) {
    return {kProject, workshop::ProjectItemId(item)};
}

ProjectOpenItem openItem(i64 id,
                         ProjectOpenItemType type,
                         std::optional<i64> parent = std::nullopt) {
    ProjectOpenItem item;
    item.id = id;
    item.projectId = kProject.value;
    item.itemType = type;
    item.parentItemId = parent;
    item.status = ProjectOpenItemStatus::Ready;
    return item;
}

ProjectOpenItem program(i64 id, i64 sourceId,
                        std::optional<i64> parent = std::nullopt) {
    auto item = openItem(id, ProjectOpenItemType::Gcode, parent);
    item.sourceTable = "gcode_files";
    item.sourceId = sourceId;
    return item;
}

ProjectOpenItem job(i64 id,
                    i64 sourceId,
                    i64 parent,
                    ProjectOpenItemStatus status = ProjectOpenItemStatus::Sent,
                    std::string persistedStatus = "running") {
    auto item = openItem(id, ProjectOpenItemType::Job, parent);
    item.sourceTable = "cnc_jobs";
    item.sourceId = sourceId;
    item.status = status;
    item.snapshotJson = "{\"job_status\":\"" + persistedStatus + "\"}";
    return item;
}

std::vector<ProjectOpenItem> protectedItems() {
    auto operation = openItem(10, ProjectOpenItemType::Operation);
    operation.sourceTable = "direct_carve";
    return {
        operation,
        program(20, 200, 10),
        job(30, 300, 20),
    };
}

std::vector<ProjectOpenItem> standaloneItems(
    ProjectOpenItemStatus jobStatus = ProjectOpenItemStatus::Sent,
    std::string persistedStatus = "running") {
    return {
        program(20, 200),
        job(30, 300, 20, jobStatus, std::move(persistedStatus)),
    };
}

ProjectPlanRunTruthInput inputFor(
    const std::vector<ProjectOpenItem>& items,
    std::optional<ProjectPlanRunSourceSnapshot> protectedRun = std::nullopt,
    std::optional<ProjectPlanRunSourceSnapshot> advancedRun = std::nullopt) {
    return {kProject, &items, protectedRun, advancedRun};
}

TEST(ProjectPlanRunTruthAdapter, ResolvesProtectedRunToExactOperationProgramAndJob) {
    const auto items = protectedItems();
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, ref(10)};

    const auto result = adapter.resolve(inputFor(items, source));

    ASSERT_TRUE(result.resolved());
    EXPECT_EQ(result.snapshot->program, ref(20));
    EXPECT_EQ(result.snapshot->operation, ref(10));
    EXPECT_EQ(result.snapshot->job, ref(30));
    EXPECT_EQ(result.snapshot->state, project_plan::RunState::Running);
}

TEST(ProjectPlanRunTruthAdapter, ResolvesStandaloneAdvancedRunningAndPausedStates) {
    const auto items = standaloneItems();
    ProjectPlanRunTruthAdapter adapter;
    for (const auto state : {project_plan::RunState::Running,
                             project_plan::RunState::Paused}) {
        const ProjectPlanRunSourceSnapshot source{300, state, std::nullopt};
        const auto result = adapter.resolve(inputFor(items, std::nullopt, source));
        ASSERT_TRUE(result.resolved());
        EXPECT_EQ(result.snapshot->program, ref(20));
        EXPECT_FALSE(result.snapshot->operation.has_value());
        EXPECT_EQ(result.snapshot->job, ref(30));
        EXPECT_EQ(result.snapshot->state, state);
    }
}

TEST(ProjectPlanRunTruthAdapter, AdvancedRunDerivesOperationFromPersistedHierarchy) {
    const auto items = protectedItems();
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Paused, std::nullopt};

    const auto result = adapter.resolve(inputFor(items, std::nullopt, source));

    ASSERT_TRUE(result.resolved());
    EXPECT_EQ(result.snapshot->operation, ref(10));
    EXPECT_EQ(result.snapshot->state, project_plan::RunState::Paused);
}

TEST(ProjectPlanRunTruthAdapter, RejectsExpectedOperationMismatch) {
    const auto items = protectedItems();
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, ref(99)};

    const auto result = adapter.resolve(inputFor(items, source));

    EXPECT_EQ(result.status, ProjectPlanRunTruthStatus::Rejected);
    EXPECT_EQ(result.error, ProjectPlanRunTruthError::OperationMismatch);
    EXPECT_FALSE(result.snapshot.has_value());
}

TEST(ProjectPlanRunTruthAdapter, RejectsConcurrentExecutionSurfaceClaims) {
    const auto items = standaloneItems();
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, std::nullopt};

    const auto result = adapter.resolve(inputFor(items, source, source));

    EXPECT_EQ(result.error,
              ProjectPlanRunTruthError::ConflictingActiveSources);
    EXPECT_FALSE(result.snapshot.has_value());
}

TEST(ProjectPlanRunTruthAdapter, RejectsJobAndHierarchyMismatches) {
    auto items = standaloneItems();
    items.back().status = ProjectOpenItemStatus::Complete;
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, std::nullopt};

    EXPECT_EQ(adapter.resolve(inputFor(items, std::nullopt, source)).error,
              ProjectPlanRunTruthError::JobStateMismatch);

    items.back().status = ProjectOpenItemStatus::Sent;
    items.back().parentItemId = 999;
    EXPECT_EQ(adapter.resolve(inputFor(items, std::nullopt, source)).error,
              ProjectPlanRunTruthError::ProgramMissing);
}

TEST(ProjectPlanRunTruthAdapter, RejectsAnotherPersistedSentJob) {
    auto items = standaloneItems();
    items.push_back(job(31, 301, 20));
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, std::nullopt};

    const auto result = adapter.resolve(inputFor(items, std::nullopt, source));

    EXPECT_EQ(result.error, ProjectPlanRunTruthError::ConflictingSavedJobs);
    EXPECT_FALSE(result.snapshot.has_value());
}

TEST(ProjectPlanRunTruthAdapter, StartupRecoveredInterruptedRunIsReviewable) {
    const auto items = standaloneItems(ProjectOpenItemStatus::Stale,
                                       "interrupted");
    ProjectPlanRunTruthAdapter adapter;
    adapter.rememberInterruptedJob(300);

    const auto result = adapter.resolve(inputFor(items));

    ASSERT_TRUE(result.resolved());
    EXPECT_EQ(result.snapshot->program, ref(20));
    EXPECT_FALSE(result.snapshot->operation.has_value());
    EXPECT_EQ(result.snapshot->job, ref(30));
    EXPECT_EQ(result.snapshot->state, project_plan::RunState::Interrupted);
}

TEST(ProjectPlanRunTruthAdapter, AbortedHistoryIsNotReportedAsStartupInterruption) {
    const auto items = standaloneItems(ProjectOpenItemStatus::Stale, "aborted");
    ProjectPlanRunTruthAdapter adapter;
    adapter.rememberInterruptedJob(300);

    const auto result = adapter.resolve(inputFor(items));

    EXPECT_EQ(result.status, ProjectPlanRunTruthStatus::None);
    EXPECT_FALSE(result.snapshot.has_value());
}

TEST(ProjectPlanRunTruthAdapter, MultipleRecoveredRunsRequireReconciliation) {
    auto items = standaloneItems(ProjectOpenItemStatus::Stale, "interrupted");
    items.push_back(program(21, 201));
    items.push_back(job(31, 301, 21, ProjectOpenItemStatus::Stale,
                        "interrupted"));
    ProjectPlanRunTruthAdapter adapter;
    adapter.rememberInterruptedJob(300);
    adapter.rememberInterruptedJob(301);

    const auto result = adapter.resolve(inputFor(items));

    EXPECT_EQ(result.error,
              ProjectPlanRunTruthError::MultipleInterruptedRuns);
    EXPECT_FALSE(result.snapshot.has_value());
}

TEST(ProjectPlanRunTruthAdapter, DisabledAdapterEmitsNoRunTruth) {
    const auto items = standaloneItems();
    ProjectPlanRunTruthAdapter adapter({false});
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, std::nullopt};

    const auto result = adapter.resolve(inputFor(items, std::nullopt, source));

    EXPECT_EQ(result.status, ProjectPlanRunTruthStatus::Disabled);
    EXPECT_FALSE(result.snapshot.has_value());
}

TEST(ProjectPlanRunActionRoute, ProtectedInterruptedJobRoutesThroughOperation) {
    auto items = protectedItems();
    items.back().status = ProjectOpenItemStatus::Stale;

    const auto route = resolveProjectPlanRunActionRoute(
        kProject, items, ref(30));

    ASSERT_TRUE(route.ready());
    EXPECT_EQ(route.surface, ProjectPlanRunActionSurface::DirectCarve);
    EXPECT_EQ(route.program, ref(20));
    EXPECT_EQ(route.operation, ref(10));
    EXPECT_EQ(route.jobSourceId, 300);
}

TEST(ProjectPlanRunActionRoute, StandaloneInterruptedJobRoutesThroughProgram) {
    const auto items = standaloneItems(ProjectOpenItemStatus::Stale,
                                       "interrupted");

    const auto route = resolveProjectPlanRunActionRoute(
        kProject, items, ref(30));

    ASSERT_TRUE(route.ready());
    EXPECT_EQ(route.surface, ProjectPlanRunActionSurface::GCodeSender);
    EXPECT_EQ(route.program, ref(20));
    EXPECT_FALSE(route.operation.has_value());
    EXPECT_EQ(route.jobSourceId, 300);
}

TEST(ProjectPlanRunActionRoute, ImportedOperationGroupRoutesThroughGCodeProgram) {
    auto operation = openItem(10, ProjectOpenItemType::Operation);
    operation.sourceTable = "operation_groups";
    const std::vector<ProjectOpenItem> items = {
        operation,
        program(20, 200, 10),
        job(30, 300, 20),
    };

    const auto route = resolveProjectPlanRunActionRoute(
        kProject, items, ref(30));

    ASSERT_TRUE(route.ready());
    EXPECT_EQ(route.surface, ProjectPlanRunActionSurface::GCodeSender);
    EXPECT_EQ(route.program, ref(20));
    EXPECT_FALSE(route.operation.has_value());
}

TEST(ProjectPlanRunTruthAdapter, ProtectedSourceRejectsImportedOperationGroup) {
    auto operation = openItem(10, ProjectOpenItemType::Operation);
    operation.sourceTable = "operation_groups";
    const std::vector<ProjectOpenItem> items = {
        operation,
        program(20, 200, 10),
        job(30, 300, 20),
    };
    ProjectPlanRunTruthAdapter adapter;
    const ProjectPlanRunSourceSnapshot source{
        300, project_plan::RunState::Running, ref(10)};

    EXPECT_EQ(adapter.resolve(inputFor(items, source)).error,
              ProjectPlanRunTruthError::OperationMismatch);
}

TEST(ProjectPlanRunActionRoute, RejectsForeignOrBrokenJobHierarchy) {
    auto items = standaloneItems();
    EXPECT_EQ(resolveProjectPlanRunActionRoute(
                  kProject, items,
                  {workshop::ProjectId(8), workshop::ProjectItemId(30)})
                  .error,
              ProjectPlanRunActionRouteError::InvalidTarget);

    items.back().parentItemId = 999;
    EXPECT_EQ(resolveProjectPlanRunActionRoute(kProject, items, ref(30)).error,
              ProjectPlanRunActionRouteError::ProgramMissing);
}

} // namespace
