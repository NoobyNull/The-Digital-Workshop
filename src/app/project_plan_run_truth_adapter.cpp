#include "project_plan_run_truth_adapter.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace dw {
namespace {

using project_plan::LiveRunSnapshot;

ProjectPlanRunTruthResult rejected(ProjectPlanRunTruthError error) {
    return {ProjectPlanRunTruthStatus::Rejected, error, std::nullopt};
}

const ProjectOpenItem* findByItemId(const std::vector<ProjectOpenItem>& items,
                                    workshop::ProjectId project,
                                    i64 itemId) {
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.projectId == project.value && item.id == itemId;
    });
    return found == items.end() ? nullptr : &*found;
}

std::vector<const ProjectOpenItem*> findJobs(
    const std::vector<ProjectOpenItem>& items,
    workshop::ProjectId project,
    i64 jobSourceId) {
    std::vector<const ProjectOpenItem*> matches;
    for (const auto& item : items) {
        if (item.projectId == project.value &&
            item.itemType == ProjectOpenItemType::Job &&
            item.sourceTable == "cnc_jobs" && item.sourceId &&
            *item.sourceId == jobSourceId) {
            matches.push_back(&item);
        }
    }
    return matches;
}

bool persistedInterrupted(const ProjectOpenItem& job) {
    if (job.status != ProjectOpenItemStatus::Stale) return false;
    const auto snapshot = nlohmann::json::parse(job.snapshotJson, nullptr, false);
    return snapshot.is_object() &&
           snapshot.value("job_status", std::string()) == "interrupted";
}

enum class HierarchyError {
    None,
    ProgramMissing,
    InvalidOperation,
};

struct JobHierarchy {
    const ProjectOpenItem* program = nullptr;
    const ProjectOpenItem* operation = nullptr;
};

HierarchyError resolveHierarchy(const std::vector<ProjectOpenItem>& items,
                                workshop::ProjectId project,
                                const ProjectOpenItem& job,
                                JobHierarchy& hierarchy) {
    if (!job.parentItemId) return HierarchyError::ProgramMissing;
    hierarchy.program = findByItemId(items, project, *job.parentItemId);
    if (!hierarchy.program ||
        hierarchy.program->itemType != ProjectOpenItemType::Gcode ||
        hierarchy.program->sourceTable != "gcode_files" ||
        !hierarchy.program->sourceId || *hierarchy.program->sourceId <= 0) {
        return HierarchyError::ProgramMissing;
    }
    if (!hierarchy.program->parentItemId) return HierarchyError::None;
    hierarchy.operation = findByItemId(items, project,
                                       *hierarchy.program->parentItemId);
    if (!hierarchy.operation ||
        hierarchy.operation->itemType != ProjectOpenItemType::Operation) {
        return HierarchyError::InvalidOperation;
    }
    return HierarchyError::None;
}

ProjectPlanRunTruthResult resolveJob(
    workshop::ProjectId project,
    const std::vector<ProjectOpenItem>& items,
    i64 jobSourceId,
    project_plan::RunState state,
    const std::optional<workshop::ProjectItemRef>& expectedOperation) {
    const auto jobs = findJobs(items, project, jobSourceId);
    if (jobs.empty())
        return rejected(ProjectPlanRunTruthError::JobNotFound);
    if (jobs.size() != 1)
        return rejected(ProjectPlanRunTruthError::AmbiguousJob);

    const auto& job = *jobs.front();
    const bool interrupted = state == project_plan::RunState::Interrupted;
    if ((interrupted && !persistedInterrupted(job)) ||
        (!interrupted && job.status != ProjectOpenItemStatus::Sent)) {
        return rejected(ProjectPlanRunTruthError::JobStateMismatch);
    }
    const bool anotherSentJob = std::any_of(
        items.begin(), items.end(), [&](const ProjectOpenItem& item) {
            return item.projectId == project.value &&
                   item.itemType == ProjectOpenItemType::Job &&
                   item.status == ProjectOpenItemStatus::Sent &&
                   item.id != job.id;
        });
    if (anotherSentJob)
        return rejected(ProjectPlanRunTruthError::ConflictingSavedJobs);
    JobHierarchy hierarchy;
    const auto hierarchyError = resolveHierarchy(items, project, job, hierarchy);
    if (hierarchyError == HierarchyError::ProgramMissing)
        return rejected(ProjectPlanRunTruthError::ProgramMissing);
    if (hierarchyError == HierarchyError::InvalidOperation)
        return rejected(ProjectPlanRunTruthError::HierarchyMismatch);

    std::optional<workshop::ProjectItemRef> operation;
    if (hierarchy.operation) {
        operation = workshop::ProjectItemRef{
            project, workshop::ProjectItemId(hierarchy.operation->id)};
    }

    if (expectedOperation) {
        if (!expectedOperation->valid() || expectedOperation->project != project ||
            operation != expectedOperation || !hierarchy.operation ||
            hierarchy.operation->sourceTable != "direct_carve") {
            return rejected(ProjectPlanRunTruthError::OperationMismatch);
        }
    }

    LiveRunSnapshot snapshot;
    snapshot.program = {project, workshop::ProjectItemId(hierarchy.program->id)};
    snapshot.operation = operation;
    snapshot.job = {project, workshop::ProjectItemId(job.id)};
    snapshot.state = state;
    return {ProjectPlanRunTruthStatus::Resolved,
            ProjectPlanRunTruthError::None,
            snapshot};
}

} // namespace

ProjectPlanRunTruthAdapter::ProjectPlanRunTruthAdapter(
    ProjectPlanRunTruthAdapterOptions options) noexcept
    : m_options(options) {}

void ProjectPlanRunTruthAdapter::rememberInterruptedJob(i64 jobSourceId) {
    if (jobSourceId <= 0 ||
        std::find(m_interruptedJobIds.begin(), m_interruptedJobIds.end(),
                  jobSourceId) != m_interruptedJobIds.end()) {
        return;
    }
    m_interruptedJobIds.push_back(jobSourceId);
}

ProjectPlanRunTruthResult ProjectPlanRunTruthAdapter::resolve(
    const ProjectPlanRunTruthInput& input) const {
    if (!m_options.enabled)
        return {ProjectPlanRunTruthStatus::Disabled,
                ProjectPlanRunTruthError::None, std::nullopt};
    if (!input.project.valid() || !input.items)
        return rejected(ProjectPlanRunTruthError::InvalidProject);
    if (input.protectedRun && input.advancedRun)
        return rejected(ProjectPlanRunTruthError::ConflictingActiveSources);

    const auto& items = *input.items;
    const auto source = input.protectedRun ? input.protectedRun : input.advancedRun;
    if (source) {
        if (!source->valid() || source->state == project_plan::RunState::Interrupted)
            return rejected(ProjectPlanRunTruthError::InvalidSource);
        return resolveJob(input.project, items, source->jobSourceId,
                          source->state, source->expectedOperation);
    }

    std::optional<LiveRunSnapshot> interrupted;
    for (const auto jobId : m_interruptedJobIds) {
        const auto jobs = findJobs(items, input.project, jobId);
        if (jobs.empty()) continue;
        if (jobs.size() != 1)
            return rejected(ProjectPlanRunTruthError::AmbiguousJob);
        if (!persistedInterrupted(*jobs.front())) continue;
        const auto resolved = resolveJob(input.project, items, jobId,
                                         project_plan::RunState::Interrupted,
                                         std::nullopt);
        if (!resolved.resolved()) return resolved;
        if (interrupted)
            return rejected(ProjectPlanRunTruthError::MultipleInterruptedRuns);
        interrupted = resolved.snapshot;
    }
    if (!interrupted)
        return {ProjectPlanRunTruthStatus::None,
                ProjectPlanRunTruthError::None, std::nullopt};
    return {ProjectPlanRunTruthStatus::Resolved,
            ProjectPlanRunTruthError::None, interrupted};
}

ProjectPlanRunActionRoute resolveProjectPlanRunActionRoute(
    workshop::ProjectId project,
    const std::vector<ProjectOpenItem>& items,
    workshop::ProjectItemRef jobRef) {
    ProjectPlanRunActionRoute route;
    if (!project.valid() || !jobRef.valid() || jobRef.project != project)
        return route;

    const auto* job = findByItemId(items, project, jobRef.item.value);
    if (!job || job->itemType != ProjectOpenItemType::Job ||
        job->sourceTable != "cnc_jobs" || !job->sourceId ||
        *job->sourceId <= 0) {
        route.error = ProjectPlanRunActionRouteError::JobMissing;
        return route;
    }
    JobHierarchy hierarchy;
    const auto hierarchyError = resolveHierarchy(items, project, *job, hierarchy);
    if (hierarchyError == HierarchyError::ProgramMissing) {
        route.error = ProjectPlanRunActionRouteError::ProgramMissing;
        return route;
    }
    if (hierarchyError == HierarchyError::InvalidOperation) {
        route.error = ProjectPlanRunActionRouteError::HierarchyMismatch;
        return route;
    }

    route.job = jobRef;
    route.program = {project, workshop::ProjectItemId(hierarchy.program->id)};
    route.jobSourceId = *job->sourceId;
    if (hierarchy.operation &&
        hierarchy.operation->sourceTable == "direct_carve") {
        route.operation = workshop::ProjectItemRef{
            project, workshop::ProjectItemId(hierarchy.operation->id)};
        route.surface = ProjectPlanRunActionSurface::DirectCarve;
    }
    route.error = ProjectPlanRunActionRouteError::None;
    return route;
}

} // namespace dw
