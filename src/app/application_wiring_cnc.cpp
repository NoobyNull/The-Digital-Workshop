// Application wiring — CNC, GCode, and tool panels.

#include "app/application.h"
#include "app/direct_carve_run_effect_adapter.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "core/cam/cam_engine_runtime.h"
#include "core/cnc/cnc_controller.h"
#include "core/cnc/macro_manager.h"
#include "core/cnc/machine_units.h"
#include "core/cnc/unified_settings.h"
#include "core/config/config.h"
#include "core/gcode/gcode_document.h"
#include "core/database/gcode_repository.h"
#include "core/database/job_repository.h"
#include "core/database/tool_database.h"
#include "core/database/toolbox_repository.h"
#include "core/materials/material_manager.h"
#include "core/paths/app_paths.h"
#include "core/paths/path_resolver.h"
#include "core/threading/main_thread_queue.h"
#include "managers/ui_manager.h"
#include "modules/project_session/project_session.h"
#include "ui/dialogs/supplier_tool_import_dialog.h"
#include "ui/panels/cam_placeholder_panel.h"
#include "ui/panels/cnc_console_panel.h"
#include "ui/panels/cnc_jog_panel.h"
#include "ui/panels/cnc_job_panel.h"
#include "ui/panels/cnc_macro_panel.h"
#include "ui/panels/cnc_safety_panel.h"
#include "ui/panels/cnc_settings_panel.h"
#include "ui/panels/cnc_status_panel.h"
#include "ui/panels/cnc_tool_panel.h"
#include "ui/panels/cnc_wcs_panel.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/tool_browser_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/panels/cost_panel.h"
#include "ui/widgets/toast.h"
#include "core/database/cut_plan_repository.h"
#include "core/optimizer/cut_list_file.h"
#include "core/project/costing_engine.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"

namespace dw {

void Application::wireCncPanels() {
    // Wire CutOptimizerPanel
    if (auto* cop = m_uiManager->cutOptimizerPanel()) {
        cop->setCutListFile(m_cutListFile.get());
        cop->setProjectManager(m_projectManager.get());
        cop->setModelRepository(m_modelRepo.get());
        cop->setMaterialManager(m_materialManager.get());
        cop->setOnAddToCost([this](const std::vector<CutOptimizerPanel::CloGroupCostData>& groups) {
            auto* costPanel = m_uiManager->costPanel();
            if (!costPanel) return;

            for (const auto& group : groups) {
                if (group.costPerSheet <= 0.0) {
                    ToastManager::instance().show(ToastType::Warning, "Skipped",
                        group.materialName + " -- no pricing available");
                    continue;
                }

                auto entry = CostingEngine::createMaterialEntry(
                    group.materialName,
                    group.stockSizeDbId,
                    group.dimensions,
                    group.costPerSheet,
                    static_cast<f64>(group.sheetsUsed),
                    "sheet");
                entry.notes = "[auto:clo]";
                costPanel->addCloEntry(entry);
            }
            costPanel->save();
        });
    }

    // Wire GCode panel and CNC subsystem
    if (auto* gcp = m_uiManager->gcodePanel()) {
        gcp->setGCodeRepository(m_gcodeRepo.get());
        gcp->setJobRepository(m_jobRepo.get());
        gcp->setProjectManager(m_projectManager.get());
        gcp->setCncController(m_cncController.get());
        gcp->setToolDatabase(m_toolDatabase.get());
        gcp->setOpenMachineProfilesCallback([this]() {
            m_uiManager->openMachineProfiles();
        });
        m_uiManager->addMachineProfileChangedCallback([gcp]() {
            gcp->onMachineProfileChanged();
        });

        // Forward program load/clear to viewport for toolpath rendering + simulation
        gcp->setOnProgramLoaded([this](const gcode::Program& prog,
                                      const gcode::Statistics& stats) {
            if (auto* vp = m_uiManager->viewportPanel()) {
                vp->setGCodeProgram(
                    prog, ViewportGCodeSource::FileBackedProgram);
                vp->setGCodeStatistics(stats);
            }
        });
        gcp->setOnProgramCleared([this]() {
            if (auto* vp = m_uiManager->viewportPanel())
                vp->clearGCodeProgram();
        });

        // Gather CNC panel pointers
        auto* csp = m_uiManager->cncStatusPanel();
        auto* jogp = m_uiManager->cncJogPanel();
        auto* conp = m_uiManager->cncConsolePanel();
        auto* wcsp = m_uiManager->cncWcsPanel();
        auto* jobp = m_uiManager->cncJobPanel();
        auto* ctp = m_uiManager->cncToolPanel();
        auto* safetyp = m_uiManager->cncSafetyPanel();
        auto* settsp = m_uiManager->cncSettingsPanel();
        auto* macrop = m_uiManager->cncMacroPanel();
        auto* vpp = m_uiManager->viewportPanel();

        // No production dispatcher calls execute() until the CAM rebuild
        // restores a protected-run initiator; only snapshot() is read today.
        if (m_projectSession && m_projectManager && m_jobRepo && m_cncController) {
            m_directCarveRunEffectAdapter =
                std::make_unique<DirectCarveRunEffectAdapter>(
                    *m_projectSession, *m_projectManager, *m_jobRepo, *m_cncController);
        }

        // Set CncController on CNC panels
        if (csp) csp->setCncController(m_cncController.get());
        if (jogp) jogp->setCncController(m_cncController.get());
        if (conp) conp->setCncController(m_cncController.get());
        if (wcsp) wcsp->setCncController(m_cncController.get());
        if (safetyp) safetyp->setCncController(m_cncController.get());
        if (settsp) {
            settsp->setCncController(m_cncController.get());
            settsp->setFileDialog(m_uiManager->fileDialog());
            settsp->setOpenMachineProfilesCallback([this]() {
                m_uiManager->openMachineProfiles();
            });
        }
        if (macrop) {
            macrop->setCncController(m_cncController.get());
            macrop->setMacroManager(m_macroManager.get());
        }

        CncCallbacks cncCb;
        cncCb.onConnectionChanged =
            [this, gcp, csp, jogp, conp, wcsp, jobp, safetyp, settsp, macrop, vpp](
                bool connected, const std::string& version) {
            gcp->onGrblConnected(connected, version);
            if (csp) csp->onConnectionChanged(connected, version);
            if (jogp) jogp->onConnectionChanged(connected, version);
            if (conp) conp->onConnectionChanged(connected, version);
            if (wcsp) wcsp->onConnectionChanged(connected, version);
            if (jobp && !connected) jobp->setStreaming(false);
            if (safetyp) safetyp->onConnectionChanged(connected, version);
            if (safetyp && !connected) {
                safetyp->setStreaming(false);
                safetyp->setProgram({});
            }
            if (settsp) settsp->onConnectionChanged(connected, version);
            if (macrop) macrop->onConnectionChanged(connected, version);
            if (vpp) vpp->setCncConnected(connected);
            m_uiManager->setCncConnected(connected);
            m_uiManager->setCncSimulating(m_cncController->isSimulating());
            if (!connected && m_wasRealConnection) {
                m_wasRealConnection = false;
                m_lastConnectedPort.clear();
                m_cncController->connectSimulator();
            }
        };
        cncCb.onStatusUpdate =
            [gcp, csp, jogp, wcsp, jobp, ctp, safetyp, settsp, macrop, vpp](
                const MachineStatus& status) {
            gcp->onGrblStatus(status);
            if (csp) csp->onStatusUpdate(status);
            if (jogp) jogp->onStatusUpdate(status);
            if (wcsp) wcsp->onStatusUpdate(status);
            if (jobp) {
                jobp->onStatusUpdate(status);
                if (ctp && ctp->hasCalcResult())
                    jobp->setRecommendedFeedRate(ctp->getRecommendedFeedRate());
            }
            if (safetyp) safetyp->onStatusUpdate(status);
            if (settsp) settsp->onStatusUpdate(status);
            if (macrop) macrop->onStatusUpdate(status);
            if (vpp) vpp->onCncStatusUpdate(status);
        };
        cncCb.onLineAcked = [gcp](const LineAck& ack) {
            gcp->onGrblLineAcked(ack);
        };
        cncCb.onProgressUpdate =
            [this, gcp, jobp, safetyp](const StreamProgress& progress) {
            gcp->onGrblProgress(progress);
            const bool streaming = progress.streaming;
            // DirectCarve-origin streams return with the CAM rebuild.
            m_uiManager->setCncStreaming(streaming, CncStreamOrigin::ExternalGCode);
            if (jobp) {
                jobp->onProgressUpdate(progress);
                jobp->setStreaming(streaming);
            }
            if (safetyp) {
                safetyp->setStreaming(streaming);
                if (!safetyp->hasProgram() && gcp->hasGCode()) {
                    safetyp->setProgram(gcp->getRawLines());
                    safetyp->setProgramBounds(gcp->boundsMin(), gcp->boundsMax());
                }
            }
        };
        cncCb.onAlarm = [gcp, csp, conp](int code, const std::string& desc) {
            gcp->onGrblAlarm(code, desc);
            if (csp) csp->onAlarm(code, desc);
            if (conp) conp->onAlarm(code, desc);
        };
        cncCb.onError = [gcp, conp](const std::string& message) {
            gcp->onGrblError(message);
            if (conp) conp->onError(message);
        };
        cncCb.onStreamingError =
            [gcp, conp](const StreamingError& error) {
                const std::string message =
                    "Streaming stopped at line " + std::to_string(error.lineIndex + 1) +
                    ": " + error.errorMessage;
                gcp->onGrblError(message);
                if (conp) conp->onError(message);
            };
        cncCb.onRawLine = [this, gcp, conp, wcsp, settsp](
            const std::string& line, bool isSent) {
            gcp->onGrblRawLine(line, isSent);
            if (conp) conp->onRawLine(line, isSent);
            if (wcsp) wcsp->onRawLine(line, isSent);
            if (settsp) {
                settsp->onRawLine(line, isSent);
                if (!isSent && line == "ok" && settsp->hasSettings()) {
                    auto& cfg = Config::instance();
                    auto profile = cfg.getActiveMachineProfile();
                    const auto& us = settsp->unifiedSettings();
                    if (m_cncController) {
                        m_cncController->setSendUnits(cnc::sendUnitsFromUnifiedSettings(us));
                    }
                    bool changed = false;
                    auto syncSetting = [&](const char* key, float& field) {
                        if (auto* s = us.get(key)) {
                            if (!s->value.empty()) {
                                try {
                                    float v = std::stof(s->value);
                                    if (v != field) {
                                        field = v;
                                        changed = true;
                                    }
                                } catch (...) {}
                            }
                        }
                    };
                    syncSetting("max_feed_x",        profile.maxFeedRateX);
                    syncSetting("max_feed_y",        profile.maxFeedRateY);
                    syncSetting("max_feed_z",        profile.maxFeedRateZ);
                    syncSetting("accel_x",           profile.accelX);
                    syncSetting("accel_y",           profile.accelY);
                    syncSetting("accel_z",           profile.accelZ);
                    syncSetting("max_travel_x",      profile.maxTravelX);
                    syncSetting("max_travel_y",      profile.maxTravelY);
                    syncSetting("max_travel_z",      profile.maxTravelZ);
                    syncSetting("junction_deviation", profile.junctionDeviation);
                    if (changed) {
                        cfg.updateMachineProfile(cfg.getActiveMachineProfileIndex(), profile);
                        cfg.save();
                    }
                }
            }
        };
        m_cncController->setCallbacks(cncCb);

        // Wire menu bar Connect/Disconnect/Panic buttons
        m_uiManager->setOnConnect([this](const std::string& port) {
            m_cncController->disconnect();
            m_cncController->connect(port, 115200);
            m_wasRealConnection = true;
            m_lastConnectedPort = port;
        });

        m_uiManager->setOnDisconnect([this]() {
            m_wasRealConnection = false;
            m_lastConnectedPort.clear();
            m_cncController->disconnect();
            m_cncController->connectSimulator();
        });

        m_uiManager->setOnPanicStop([this]() {
            m_cncController->feedHold();
            if (m_cncController->isStreaming()) m_cncController->stopStream();
            ToastManager::instance().show(
                ToastType::Warning, "PANIC STOP", "Feed hold sent — job aborted", 5.0f);
        });
    }

    // Wire tool panels
    if (auto* tbp = m_uiManager->toolBrowserPanel()) {
        tbp->setToolDatabase(m_toolDatabase.get());
        tbp->setToolboxRepository(m_toolboxRepo.get());
        tbp->setMaterialManager(m_materialManager.get());
        tbp->setFileDialog(m_uiManager->fileDialog());
        if (auto* importDialog = m_uiManager->supplierToolImportDialog()) {
            importDialog->setToolDatabase(m_toolDatabase.get());
            importDialog->setToolboxRepository(m_toolboxRepo.get());
            importDialog->setOnImported([tbp](const SelectiveToolImportResult&) {
                tbp->refresh();
            });
            tbp->setSupplierToolImportDialog(importDialog);
        }
        tbp->setOpenMachineProfilesCallback([this]() {
            m_uiManager->openMachineProfiles();
        });
        m_uiManager->addMachineProfileChangedCallback([tbp]() {
            tbp->onMachineProfileChanged();
        });
    }
    if (auto* ctp = m_uiManager->cncToolPanel()) {
        ctp->setToolDatabase(m_toolDatabase.get());
        ctp->setMaterialManager(m_materialManager.get());
    }

    // Wire the CAM panel's early workflow surface (v0.8.0 slice): async
    // engine lifecycle, default-surfacing generation, and Run handoff.
    if (auto* camp = m_uiManager->camPlaceholderPanel()) {
        camp->setEngineStatusProvider([this]() -> std::string {
            {
                std::lock_guard<std::mutex> lock(m_camGeneration->mutex);
                if (m_camGeneration->running && !m_camGeneration->message.empty())
                    return m_camGeneration->message;
            }
            if (!m_camEngineStatus)
                return "Engine not started";
            // Don't keep reporting "ready" after the owned child dies. Only
            // probed while no worker owns the runtime.
            if (m_camEngineStatus->ready && m_camEngineRuntime &&
                m_camEngineRuntime->ownedChildExited()) {
                m_camEngineStatus->ready = false;
                m_camEngineStatus->reason = "CAM engine stopped (process exited)";
            }
            return m_camEngineStatus->ready
                       ? "Engine ready at " + m_camEngineStatus->endpoint
                       : m_camEngineStatus->reason;
        });
        camp->setOnStartEngine([this]() { startCamEngineAsync(); });
        camp->setActiveSetupProvider([this]() -> std::string {
            return m_camActiveSetup ? m_camActiveSetup->modelName : std::string{};
        });
        camp->setJobStatusProvider([this]() -> std::string {
            std::lock_guard<std::mutex> lock(m_camGeneration->mutex);
            return m_camGeneration->message;
        });
        camp->setMachinesProvider([this]() { return m_camMachines; });
        camp->setOnGenerate([this](const std::string& machineId) {
            startCamGenerationAsync(machineId);
        });
        camp->setRunHandoffReadyProvider([this]() {
            return m_camGeneratedItemId.has_value();
        });
        camp->setOnSendToRun([this]() { sendGeneratedCamGCodeToRun(); });
    }
}

} // namespace dw
