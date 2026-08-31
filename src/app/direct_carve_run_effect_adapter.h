#pragma once

#include <optional>
#include <string>

#include "core/types.h"
#include "modules/run_coordination/run_coordinator.h"

namespace dw {

class CncController;
class JobRepository;
class ProjectManager;

namespace workshop {
class ProjectSession;
}

enum class DirectCarveRunEffectStatus {
    Applied,
    Unchanged,
    Rejected,
};

enum class DirectCarveRunEffectError {
    None,
    InvalidIdentity,
    ActiveRunConflict,
    ProjectContextMismatch,
    LockAcquireRejected,
    NoActiveRun,
    RunIdentityMismatch,
    RunPackageMismatch,
    GCodeItemMissing,
    GCodeItemInvalid,
    GCodeHierarchyMismatch,
    GCodeSnapshotInvalid,
    GCodeFileMissing,
    GCodeFileEmpty,
    GCodeFingerprintMismatch,
    StreamStartFailed,
    JobRecordInsertFailed,
    JobRecordUnavailable,
    InvalidControlState,
    JobFinalizeFailed,
    LockReleaseRejected,
};

struct DirectCarveRunEffectResult {
    DirectCarveRunEffectStatus status = DirectCarveRunEffectStatus::Rejected;
    DirectCarveRunEffectError error = DirectCarveRunEffectError::None;
    std::optional<i64> jobId;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == DirectCarveRunEffectError::None &&
               status != DirectCarveRunEffectStatus::Rejected;
    }
};

enum class DirectCarveRunControlState {
    Idle,
    Locked,
    Streaming,
    Paused,
    Aborting,
};

struct DirectCarveRunEffectSnapshot {
    DirectCarveRunControlState state = DirectCarveRunControlState::Idle;
    std::optional<run_coordination::RunIdentity> identity;
    std::optional<run_coordination::RunPackage> package;
    std::optional<i64> jobId;
    std::string persistedGCodePath;
    bool streamAttempted = false;
};

// Application boundary for protected Run effects. It owns only the exact
// acquired identity, package, G-code path, and job row needed to execute and
// finalize one run. It never consults a current selection as a fallback.
class DirectCarveRunEffectAdapter final {
  public:
    DirectCarveRunEffectAdapter(workshop::ProjectSession& projectSession,
                                ProjectManager& projectManager,
                                JobRepository& jobRepository,
                                CncController& cncController) noexcept;

    [[nodiscard]] DirectCarveRunEffectResult
    execute(const run_coordination::RunEffect& effect);
    [[nodiscard]] const DirectCarveRunEffectSnapshot& snapshot() const noexcept;

  private:
    [[nodiscard]] DirectCarveRunEffectResult
    handle(const run_coordination::AcquireRunLock& effect);
    [[nodiscard]] DirectCarveRunEffectResult
    handle(const run_coordination::StartStream& effect);
    [[nodiscard]] DirectCarveRunEffectResult
    handle(const run_coordination::FeedHold& effect);
    [[nodiscard]] DirectCarveRunEffectResult
    handle(const run_coordination::CycleStart& effect);
    [[nodiscard]] DirectCarveRunEffectResult
    handle(const run_coordination::AbortStream& effect);
    [[nodiscard]] DirectCarveRunEffectResult
    handle(const run_coordination::ReleaseRunLock& effect);

    [[nodiscard]] bool matchesActive(
        const run_coordination::RunIdentity& identity) const noexcept;
    [[nodiscard]] bool sessionHasExactLock(
        const run_coordination::RunIdentity& identity) const noexcept;
    [[nodiscard]] DirectCarveRunEffectResult rejected(
        DirectCarveRunEffectError error) const noexcept;
    [[nodiscard]] DirectCarveRunEffectResult applied() const noexcept;
    [[nodiscard]] DirectCarveRunEffectResult unchanged() const noexcept;

    workshop::ProjectSession& m_projectSession;
    ProjectManager& m_projectManager;
    JobRepository& m_jobRepository;
    CncController& m_cncController;
    DirectCarveRunEffectSnapshot m_snapshot;
    std::optional<workshop::RunId> m_lastReleasedRun;
};

} // namespace dw
