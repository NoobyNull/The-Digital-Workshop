#pragma once

#include <optional>
#include <vector>

#include "core/database/project_repository.h"
#include "modules/project_plan/project_plan.h"

namespace dw {

// Reporting-only snapshot from one execution surface. The source job ID is the
// cnc_jobs row ID; the adapter resolves project item identity from the current
// Project snapshot instead of trusting a mutable UI selection.
struct ProjectPlanRunSourceSnapshot {
    i64 jobSourceId = 0;
    project_plan::RunState state = project_plan::RunState::Running;
    std::optional<workshop::ProjectItemRef> expectedOperation;

    [[nodiscard]] bool valid() const noexcept { return jobSourceId > 0; }
};

struct ProjectPlanRunTruthInput {
    workshop::ProjectId project;
    const std::vector<ProjectOpenItem>* items = nullptr;
    std::optional<ProjectPlanRunSourceSnapshot> protectedRun;
    std::optional<ProjectPlanRunSourceSnapshot> advancedRun;
};

enum class ProjectPlanRunTruthStatus {
    Disabled,
    None,
    Resolved,
    Rejected,
};

enum class ProjectPlanRunTruthError {
    None,
    InvalidProject,
    InvalidSource,
    ConflictingActiveSources,
    JobNotFound,
    AmbiguousJob,
    JobStateMismatch,
    ConflictingSavedJobs,
    ProgramMissing,
    HierarchyMismatch,
    OperationMismatch,
    MultipleInterruptedRuns,
};

struct ProjectPlanRunTruthResult {
    ProjectPlanRunTruthStatus status = ProjectPlanRunTruthStatus::None;
    ProjectPlanRunTruthError error = ProjectPlanRunTruthError::None;
    std::optional<project_plan::LiveRunSnapshot> snapshot;

    [[nodiscard]] bool resolved() const noexcept {
        return status == ProjectPlanRunTruthStatus::Resolved &&
               snapshot.has_value();
    }
};

struct ProjectPlanRunTruthAdapterOptions {
    bool enabled = true;
};

enum class ProjectPlanRunActionSurface {
    DirectCarve,
    GCodeSender,
};

enum class ProjectPlanRunActionRouteError {
    None,
    InvalidTarget,
    JobMissing,
    ProgramMissing,
    HierarchyMismatch,
};

struct ProjectPlanRunActionRoute {
    ProjectPlanRunActionRouteError error =
        ProjectPlanRunActionRouteError::InvalidTarget;
    ProjectPlanRunActionSurface surface =
        ProjectPlanRunActionSurface::GCodeSender;
    workshop::ProjectItemRef job;
    workshop::ProjectItemRef program;
    std::optional<workshop::ProjectItemRef> operation;
    i64 jobSourceId = 0;

    [[nodiscard]] bool ready() const noexcept {
        return error == ProjectPlanRunActionRouteError::None;
    }
};

// Resolve the non-stale content surface behind a run-history item. This does
// not activate the Job row itself, so startup-interrupted (Stale) jobs remain
// reviewable through their owning operation or G-code program.
[[nodiscard]] ProjectPlanRunActionRoute resolveProjectPlanRunActionRoute(
    workshop::ProjectId project,
    const std::vector<ProjectOpenItem>& items,
    workshop::ProjectItemRef job);

// Unifies Project Plan run reporting for protected Direct Carve and the
// Advanced G-code sender. It emits no machine or repository commands. If both
// execution surfaces claim a run, the adapter rejects the conflict rather than
// assigning an unsafe priority.
class ProjectPlanRunTruthAdapter final {
  public:
    explicit ProjectPlanRunTruthAdapter(
        ProjectPlanRunTruthAdapterOptions options = {}) noexcept;

    void rememberInterruptedJob(i64 jobSourceId);
    [[nodiscard]] ProjectPlanRunTruthResult
    resolve(const ProjectPlanRunTruthInput& input) const;

  private:
    ProjectPlanRunTruthAdapterOptions m_options;
    std::vector<i64> m_interruptedJobIds;
};

} // namespace dw
