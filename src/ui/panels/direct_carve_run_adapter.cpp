// Thin UI adapter between the immutable RunCoordinator and application effects.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <array>
#include <type_traits>

#include "core/cnc/cnc_controller.h"
#include "ui/panels/gcode_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

using namespace run_coordination;

bool startsMachine(const RunEffect& effect) {
    return std::holds_alternative<AcquireRunLock>(effect) ||
           std::holds_alternative<StartStream>(effect);
}

} // namespace

RunPreflightFacts DirectCarvePanel::runPreflightFacts() const {
    const bool controllerIdle = m_cnc && !m_cnc->isStreaming() &&
                                m_machineStatus.state == MachineState::Idle;
    return RunPreflightFacts(std::array<bool, 7>{
        m_cncConnected,
        controllerIdle,
        m_homingVerified || m_homingSkipped,
        m_zeroConfirmed,
        m_toolSetupConfirmed,
        m_stockSecuredConfirmed,
        m_outlineCompleted || m_outlineSkipped,
    });
}

bool DirectCarvePanel::hasActiveProtectedRun() const noexcept {
    return m_runCoordinator.snapshot().live();
}

void DirectCarvePanel::requestRunStart() {
    if (!canStartCarve() || !m_preparationPin || !pinnedPreparationActive() ||
        !m_projectDirectoryRequest) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Protected Run Unavailable",
            "Keep the G-code preview/export, or reopen this setup from its Project Plan.");
        return;
    }
    if (m_runCoordinator.snapshot().live()) return;
    if (m_preparationDirty && !savePreparation()) {
        ToastManager::instance().show(
            ToastType::Error, "Preparation Not Saved",
            "The exact setup could not be saved, so no machine run was started.");
        return;
    }

    const auto pin = *m_preparationPin;
    m_projectDirectoryRequest(
        pin,
        [this, pin](std::shared_ptr<ProjectDirectory> directory) {
            saveGCodeToProjectDirectory(
                pin,
                std::move(directory),
                [this, pin](bool saved) {
                    if (!saved || !m_savedRunToolpath || !m_preparationPin ||
                        !(*m_preparationPin == pin) || !hasCurrentToolpath()) {
                        ToastManager::instance().show(
                            ToastType::Error, "Run Package Not Created",
                            "The exact generated G-code could not be pinned. No CNC command was sent.");
                        return;
                    }

                    const auto& savedToolpath = *m_savedRunToolpath;
                    ToolpathIdentity toolpath(
                        pin.operationItem(),
                        savedToolpath.gcodeItem,
                        ToolpathRevision{savedToolpath.editRevision},
                        savedToolpath.fingerprint);
                    RunSetupIdentity setup(pin, std::move(toolpath));
                    RunIdentity identity(workshop::RunId(
                                             static_cast<std::int64_t>(m_nextRunId++)),
                                         setup);
                    RunPreflightSnapshot preflight(
                        setup,
                        PreflightRevision{++m_preflightRevision},
                        runPreflightFacts());
                    RunPackage package(std::move(identity), std::move(preflight));
                    if (!package.valid()) {
                        ToastManager::instance().show(
                            ToastType::Error, "Preflight Changed",
                            "Machine or setup facts changed before Run. Review the final checks again.");
                        return;
                    }

                    const auto transition =
                        m_runCoordinator.dispatch(StartRun{std::move(package)});
                    if (!applyRunTransition(transition)) return;

                    m_currentStep = Step::Running;
                    m_maxStepVisited = std::max(
                        m_maxStepVisited, static_cast<int>(Step::Running));
                    m_runCurrentPass = "Carving";
                    if (m_cnc) {
                        const auto progress = m_cnc->streamProgress();
                        m_runTotalLines = progress.totalLines;
                        m_runCurrentLine = progress.ackedLines;
                        m_runElapsedSec = progress.elapsedSeconds;
                    }
                    if (m_gcodePanel)
                        m_gcodePanel->onCarveStreamStart(m_runTotalLines);
                });
        });
}

bool DirectCarvePanel::applyRunTransition(const RunTransition& transition) {
    if (transition.status == RunTransitionStatus::Rejected ||
        transition.status == RunTransitionStatus::Disabled) {
        ToastManager::instance().show(
            ToastType::Error, "Run Command Rejected",
            "The protected run state changed. No unapproved CNC command was sent.");
        return false;
    }

    const bool terminalTransition = std::any_of(
        transition.effects.begin(), transition.effects.end(), [](const RunEffect& effect) {
            return std::holds_alternative<ReleaseRunLock>(effect);
        });
    bool allEffectsApplied = true;
    for (const auto& effect : transition.effects) {
        if (!m_runEffectExecutor || !m_runEffectExecutor(effect)) {
            allEffectsApplied = false;
            if (terminalTransition) {
                continue; // Release must still be attempted after an abort failure.
            }
            if (startsMachine(effect) && m_runCoordinator.snapshot().live()) {
                failActiveRun(RunFailure::StreamStartFailed);
            } else if (m_runCoordinator.snapshot().live()) {
                failActiveRun(RunFailure::TransportError);
            }
            ToastManager::instance().show(
                ToastType::Error, "Run Command Failed",
                "The machine command was not accepted; the run lock was released safely.");
            return false;
        }

        if (const auto* release = std::get_if<ReleaseRunLock>(&effect)) {
            if (m_gcodePanel) {
                if (release->outcome == RunOutcome::Completed)
                    m_gcodePanel->onCarveStreamComplete();
                else
                    m_gcodePanel->onCarveStreamAborted();
            }
        }
    }
    if (!allEffectsApplied) {
        ToastManager::instance().show(
            ToastType::Error, "Run Cleanup Incomplete",
            "The stream stopped, but one cleanup action needs attention before another run.");
    }
    return allEffectsApplied;
}

void DirectCarvePanel::requestRunPause() {
    const auto* identity = m_runCoordinator.snapshot().identity();
    if (identity)
        (void)applyRunTransition(m_runCoordinator.dispatch(PauseRun{*identity}));
}

void DirectCarvePanel::requestRunResume() {
    const auto* identity = m_runCoordinator.snapshot().identity();
    if (identity)
        (void)applyRunTransition(m_runCoordinator.dispatch(ResumeRun{*identity}));
}

void DirectCarvePanel::requestRunAbort() {
    const auto* identity = m_runCoordinator.snapshot().identity();
    if (identity)
        (void)applyRunTransition(m_runCoordinator.dispatch(AbortRun{*identity}));
}

void DirectCarvePanel::failActiveRun(RunFailure failure) {
    const auto* identity = m_runCoordinator.snapshot().identity();
    if (!identity || !m_runCoordinator.snapshot().live()) return;
    const auto transition = m_runCoordinator.dispatch(
        FailRun{*identity, RunEventSequence{++m_runEventSequence}, failure});
    (void)applyRunTransition(transition);
}

void DirectCarvePanel::onRunProgress(const StreamProgress& progress) {
    const auto* identity = m_runCoordinator.snapshot().identity();
    if (!identity || !m_runCoordinator.snapshot().live()) return;

    m_runCurrentLine = progress.ackedLines;
    m_runTotalLines = progress.totalLines;
    m_runElapsedSec = progress.elapsedSeconds;
    if (m_gcodePanel) {
        m_gcodePanel->onCarveStreamProgress(
            progress.ackedLines, progress.totalLines, progress.elapsedSeconds);
    }

    const auto sequence = RunEventSequence{++m_runEventSequence};
    if (!progress.streaming && progress.totalLines > 0 &&
        progress.ackedLines >= progress.totalLines) {
        (void)applyRunTransition(
            m_runCoordinator.dispatch(CompleteRun{*identity, sequence}));
        return;
    }
    if (progress.streaming &&
        m_runCoordinator.snapshot().state == RunState::Streaming) {
        const double fraction = progress.totalLines > 0
                                    ? static_cast<double>(progress.ackedLines) /
                                          static_cast<double>(progress.totalLines)
                                    : 0.0;
        (void)applyRunTransition(m_runCoordinator.dispatch(
            RunProgressed{*identity, sequence, std::clamp(fraction, 0.0, 1.0)}));
    }
}

void DirectCarvePanel::onRunFailure(RunFailure failure) {
    failActiveRun(failure);
}

} // namespace dw
