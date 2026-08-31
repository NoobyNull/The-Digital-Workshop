#include "direct_carve_run_effect_adapter.h"

#include <fstream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/cnc/cnc_controller.h"
#include "core/database/job_repository.h"
#include "core/gcode/gcode_modal_scanner.h"
#include "core/mesh/hash.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "modules/project_session/project_session.h"

namespace dw {
namespace {

using run_coordination::RunIdentity;
using run_coordination::RunOutcome;

workshop::RunLockRef lockFor(const RunIdentity& identity) {
    return {identity.run(), identity.setup().toolpath().operationItem()};
}

bool sameLock(const workshop::RunLockRef& lhs,
              const workshop::RunLockRef& rhs) noexcept {
    return lhs.run == rhs.run && lhs.projectOperation == rhs.projectOperation;
}

bool runnableGCodeStatus(ProjectOpenItemStatus status) noexcept {
    return status == ProjectOpenItemStatus::Ready ||
           status == ProjectOpenItemStatus::Generated;
}

// Runnable lines only: blanks and comment lines dropped, inline comments and
// trailing whitespace stripped — the same filter the G-code panel streams with.
std::vector<std::string> readRunnableLines(const Path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in.is_open())
        return lines;
    std::string raw;
    while (std::getline(in, raw)) {
        while (!raw.empty() &&
               (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r'))
            raw.pop_back();
        // Trim leading whitespace so indented comment lines ("  ; note")
        // don't survive as whitespace-only entries inflating totalLines.
        const auto firstVisible = raw.find_first_not_of(" \t");
        if (firstVisible == std::string::npos)
            continue;
        raw.erase(0, firstVisible);
        if (raw.front() == ';' || raw.front() == '(')
            continue;
        const auto semi = raw.find(';');
        if (semi != std::string::npos) {
            raw = raw.substr(0, semi);
            while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
                raw.pop_back();
            if (raw.empty())
                continue;
        }
        lines.push_back(raw);
    }
    return lines;
}

const char* jobStatus(RunOutcome outcome) noexcept {
    switch (outcome) {
    case RunOutcome::Completed:
        return "completed";
    case RunOutcome::Aborted:
        return "aborted";
    case RunOutcome::Failed:
        return "interrupted";
    }
    return "interrupted";
}

} // namespace

DirectCarveRunEffectAdapter::DirectCarveRunEffectAdapter(
    workshop::ProjectSession& projectSession,
    ProjectManager& projectManager,
    JobRepository& jobRepository,
    CncController& cncController) noexcept
    : m_projectSession(projectSession), m_projectManager(projectManager),
      m_jobRepository(jobRepository),
      m_cncController(cncController) {}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::execute(
    const run_coordination::RunEffect& effect) {
    return std::visit(
        [this](const auto& typed) {
            return handle(typed);
        },
        effect);
}

const DirectCarveRunEffectSnapshot&
DirectCarveRunEffectAdapter::snapshot() const noexcept {
    return m_snapshot;
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::handle(
    const run_coordination::AcquireRunLock& effect) {
    if (!effect.identity.valid())
        return rejected(DirectCarveRunEffectError::InvalidIdentity);

    if (m_snapshot.identity.has_value()) {
        if (*m_snapshot.identity == effect.identity && sessionHasExactLock(effect.identity))
            return unchanged();
        return rejected(DirectCarveRunEffectError::ActiveRunConflict);
    }
    if (m_lastReleasedRun == effect.identity.run())
        return rejected(DirectCarveRunEffectError::ActiveRunConflict);

    const auto operationRef = effect.identity.setup().toolpath().operationItem();
    const auto currentProject = m_projectManager.currentProject();
    if (!currentProject || currentProject->id() != operationRef.project.value)
        return rejected(DirectCarveRunEffectError::ProjectContextMismatch);

    const auto operation = m_projectManager.findOpenItem(operationRef.item.value);
    if (!operation || operation->id != operationRef.item.value ||
        operation->projectId != operationRef.project.value ||
        operation->itemType != ProjectOpenItemType::Operation) {
        return rejected(DirectCarveRunEffectError::ProjectContextMismatch);
    }

    const auto before = m_projectSession.snapshot();
    if (before.activeRun.has_value())
        return rejected(DirectCarveRunEffectError::ActiveRunConflict);

    const auto transition = m_projectSession.dispatch(
        workshop::WorkshopCommand{workshop::BeginRun{lockFor(effect.identity)},
                                  before.generation});
    if (transition.status != workshop::TransitionStatus::Applied ||
        !sessionHasExactLock(effect.identity)) {
        return rejected(DirectCarveRunEffectError::LockAcquireRejected);
    }

    m_snapshot.state = DirectCarveRunControlState::Locked;
    m_snapshot.identity = effect.identity;
    return applied();
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::handle(
    const run_coordination::StartStream& effect) {
    if (!effect.package.valid())
        return rejected(DirectCarveRunEffectError::InvalidIdentity);
    if (!matchesActive(effect.package.identity()) ||
        !sessionHasExactLock(effect.package.identity())) {
        return rejected(DirectCarveRunEffectError::RunIdentityMismatch);
    }
    if (m_snapshot.package.has_value()) {
        return *m_snapshot.package == effect.package
            ? unchanged()
            : rejected(DirectCarveRunEffectError::RunPackageMismatch);
    }
    if (m_snapshot.state != DirectCarveRunControlState::Locked)
        return rejected(DirectCarveRunEffectError::InvalidControlState);

    const auto& toolpath = effect.package.identity().setup().toolpath();
    const auto gcodeRef = toolpath.gcodeItem();
    const auto operationRef = toolpath.operationItem();
    const auto item = m_projectManager.findOpenItem(gcodeRef.item.value);
    if (!item)
        return rejected(DirectCarveRunEffectError::GCodeItemMissing);
    if (item->id != gcodeRef.item.value || item->projectId != gcodeRef.project.value ||
        item->itemType != ProjectOpenItemType::Gcode ||
        item->sourceTable != "gcode_files" || !item->sourceId.has_value() ||
        !runnableGCodeStatus(item->status)) {
        return rejected(DirectCarveRunEffectError::GCodeItemInvalid);
    }
    if (!item->parentItemId.has_value() ||
        *item->parentItemId != operationRef.item.value ||
        operationRef.project != gcodeRef.project) {
        return rejected(DirectCarveRunEffectError::GCodeHierarchyMismatch);
    }

    const auto persisted = nlohmann::json::parse(item->snapshotJson, nullptr, false);
    if (persisted.is_discarded() || !persisted.contains("hash") ||
        !persisted["hash"].is_string() || !persisted.contains("file_path") ||
        !persisted["file_path"].is_string()) {
        return rejected(DirectCarveRunEffectError::GCodeSnapshotInvalid);
    }
    const std::string fingerprint = persisted["hash"].get<std::string>();
    const std::string storedPathText = persisted["file_path"].get<std::string>();
    if (fingerprint.empty() || storedPathText.empty())
        return rejected(DirectCarveRunEffectError::GCodeSnapshotInvalid);

    const Path storedPath(storedPathText);
    const Path resolvedPath = PathResolver::resolve(storedPath, PathCategory::GCode);
    const std::string actualFingerprint = hash::computeFile(resolvedPath);
    if (actualFingerprint.empty())
        return rejected(DirectCarveRunEffectError::GCodeFileMissing);
    if (fingerprint != toolpath.contentFingerprint() ||
        actualFingerprint != fingerprint) {
        return rejected(DirectCarveRunEffectError::GCodeFingerprintMismatch);
    }

    const std::vector<std::string> lines = readRunnableLines(resolvedPath);
    if (lines.empty())
        // The file exists and hash-matched; it just has no runnable commands
        // (comment/blank-only). "Missing" would misdirect diagnosis.
        return rejected(DirectCarveRunEffectError::GCodeFileEmpty);

    m_snapshot.package = effect.package;
    m_snapshot.persistedGCodePath = storedPathText;
    m_snapshot.state = DirectCarveRunControlState::Streaming;
    m_snapshot.streamAttempted = true;

    const bool streamStarted = m_cncController.startStream(lines);
    JobRecord record;
    record.fileName = storedPath.filename().string();
    record.filePath = storedPathText;
    record.totalLines = static_cast<int>(lines.size());
    const auto jobId = m_jobRepository.insert(record);
    if (!jobId.has_value()) {
        if (streamStarted) {
            m_cncController.feedHold();
            m_cncController.stopStream();
        }
        m_snapshot.state = DirectCarveRunControlState::Aborting;
        return rejected(DirectCarveRunEffectError::JobRecordInsertFailed);
    }

    m_snapshot.jobId = *jobId;
    (void)m_projectManager.listOpenItems(gcodeRef.project.value);
    if (!streamStarted) {
        // No stream exists: leaving the snapshot in Streaming would publish
        // a phantom active run. Mirror the insert-failure teardown.
        m_snapshot.state = DirectCarveRunControlState::Aborting;
        return rejected(DirectCarveRunEffectError::StreamStartFailed);
    }
    return applied();
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::handle(
    const run_coordination::FeedHold& effect) {
    if (!matchesActive(effect.identity) || !sessionHasExactLock(effect.identity))
        return rejected(DirectCarveRunEffectError::RunIdentityMismatch);
    if (m_snapshot.state == DirectCarveRunControlState::Paused)
        return unchanged();
    if (m_snapshot.state != DirectCarveRunControlState::Streaming)
        return rejected(DirectCarveRunEffectError::InvalidControlState);

    m_cncController.feedHold();
    m_snapshot.state = DirectCarveRunControlState::Paused;
    return applied();
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::handle(
    const run_coordination::CycleStart& effect) {
    if (!matchesActive(effect.identity) || !sessionHasExactLock(effect.identity))
        return rejected(DirectCarveRunEffectError::RunIdentityMismatch);
    if (m_snapshot.state == DirectCarveRunControlState::Streaming)
        return unchanged();
    if (m_snapshot.state != DirectCarveRunControlState::Paused)
        return rejected(DirectCarveRunEffectError::InvalidControlState);

    m_cncController.cycleStart();
    m_snapshot.state = DirectCarveRunControlState::Streaming;
    return applied();
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::handle(
    const run_coordination::AbortStream& effect) {
    if (!matchesActive(effect.identity) || !sessionHasExactLock(effect.identity))
        return rejected(DirectCarveRunEffectError::RunIdentityMismatch);
    if (m_snapshot.state == DirectCarveRunControlState::Locked)
        return unchanged();
    if (m_snapshot.state == DirectCarveRunControlState::Aborting)
        return unchanged();
    if (m_snapshot.state != DirectCarveRunControlState::Streaming &&
        m_snapshot.state != DirectCarveRunControlState::Paused) {
        return rejected(DirectCarveRunEffectError::InvalidControlState);
    }

    // Safety order is deliberate: hold motion, then stop the queued stream.
    // stopStream() already halts the queue. A later resume can no longer
    // outrank abort.
    m_cncController.feedHold();
    m_cncController.stopStream();
    m_snapshot.state = DirectCarveRunControlState::Aborting;
    return applied();
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::handle(
    const run_coordination::ReleaseRunLock& effect) {
    if (!m_snapshot.identity.has_value()) {
        if (m_lastReleasedRun == effect.identity.run())
            return unchanged();
        return rejected(DirectCarveRunEffectError::NoActiveRun);
    }
    if (!matchesActive(effect.identity) || !sessionHasExactLock(effect.identity))
        return rejected(DirectCarveRunEffectError::RunIdentityMismatch);
    const bool terminalAbort = effect.outcome == RunOutcome::Aborted ||
                               effect.outcome == RunOutcome::Failed;
    const bool safelyNotStarted = m_snapshot.state == DirectCarveRunControlState::Locked;
    const bool liveForCompletion =
        m_snapshot.state == DirectCarveRunControlState::Streaming ||
        m_snapshot.state == DirectCarveRunControlState::Paused;
    if ((terminalAbort && m_snapshot.state != DirectCarveRunControlState::Aborting &&
         !safelyNotStarted) || (!terminalAbort && !liveForCompletion)) {
        return rejected(DirectCarveRunEffectError::InvalidControlState);
    }

    std::optional<i64> finalizedJob;
    DirectCarveRunEffectError terminalWarning = DirectCarveRunEffectError::None;
    if (m_snapshot.jobId.has_value()) {
        if (m_snapshot.persistedGCodePath.empty())
            return rejected(DirectCarveRunEffectError::NoActiveRun);
        const StreamProgress progress = m_cncController.streamProgress();
        const Path resolvedPath = PathResolver::resolve(
            Path(m_snapshot.persistedGCodePath), PathCategory::GCode);
        const ModalState modal = GCodeModalScanner::scanToLine(
            readRunnableLines(resolvedPath), progress.ackedLines);
        finalizedJob = *m_snapshot.jobId;
        if (!m_jobRepository.finishJob(*finalizedJob,
                                       jobStatus(effect.outcome),
                                       progress.ackedLines,
                                       progress.elapsedSeconds,
                                       progress.errorCount,
                                       modal)) {
            terminalWarning = DirectCarveRunEffectError::JobFinalizeFailed;
        }
    }

    const auto sessionBefore = m_projectSession.snapshot();
    const auto transition = m_projectSession.dispatch(
        workshop::WorkshopCommand{workshop::EndRun{effect.identity.run()},
                                  sessionBefore.generation});
    if (transition.status != workshop::TransitionStatus::Applied)
        return rejected(DirectCarveRunEffectError::LockReleaseRejected);

    const auto project = effect.identity.setup().toolpath().gcodeItem().project;
    (void)m_projectManager.listOpenItems(project.value);
    m_lastReleasedRun = effect.identity.run();
    const bool jobRecordUnavailable =
        m_snapshot.streamAttempted && !finalizedJob.has_value();
    m_snapshot = {};
    DirectCarveRunEffectResult result = applied();
    result.jobId = finalizedJob;
    if (terminalWarning != DirectCarveRunEffectError::None)
        result.error = terminalWarning;
    else if (jobRecordUnavailable)
        result.error = DirectCarveRunEffectError::JobRecordUnavailable;
    return result;
}

bool DirectCarveRunEffectAdapter::matchesActive(
    const RunIdentity& identity) const noexcept {
    return m_snapshot.identity.has_value() && *m_snapshot.identity == identity;
}

bool DirectCarveRunEffectAdapter::sessionHasExactLock(
    const RunIdentity& identity) const noexcept {
    const auto active = m_projectSession.snapshot().activeRun;
    return active.has_value() && sameLock(*active, lockFor(identity));
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::rejected(
    DirectCarveRunEffectError error) const noexcept {
    return {DirectCarveRunEffectStatus::Rejected, error, m_snapshot.jobId};
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::applied() const noexcept {
    return {DirectCarveRunEffectStatus::Applied,
            DirectCarveRunEffectError::None,
            m_snapshot.jobId};
}

DirectCarveRunEffectResult DirectCarveRunEffectAdapter::unchanged() const noexcept {
    return {DirectCarveRunEffectStatus::Unchanged,
            DirectCarveRunEffectError::None,
            m_snapshot.jobId};
}

} // namespace dw
