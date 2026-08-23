// Direct Carve wizard panel -- step-by-step guided workflow for streaming
// 2.5D toolpaths directly from STL models. Each step validates before allowing
// progression. Machine readiness is verified before any carving begins.

#include "ui/panels/direct_carve_panel.h"

#include "core/gcode/gcode_document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/carve/carve_job.h"
#include "core/carve/gcode_export.h"
#include "core/carve/material_blank_defaults.h"
#include "core/carve/roughing_tool_selector.h"
#include "core/carve/toolpath_advisor.h"
#include "core/cnc/cnc_controller.h"
#include "core/cnc/machine_profile_calculator_adapter.h"
#include "core/cnc/tool_calculator.h"
#include "core/config/config.h"
#include "core/database/gcode_repository.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/database/tool_database.h"
#include "core/database/toolbox_repository.h"
#include "core/gcode/machine_profile.h"
#include "core/library/library_manager.h"
#include "core/loaders/gcode_loader.h"
#include "core/materials/material_manager.h"
#include "core/mesh/hash.h"
#include "core/paths/path_resolver.h"
#include "core/utils/file_utils.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/icons.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/theme.h"
#include "ui/tool_library_access.h"
#include "ui/ui_colors.h"
#include "ui/widgets/toast.h"

namespace dw {

namespace {

std::string fixedNumber(f32 value, int precision)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision)
        << static_cast<double>(value);
    return out.str();
}

std::string feedNumber(f32 value)
{
    return fixedNumber(value, 0);
}

std::string axisWord(char axis, f32 value)
{
    return std::string(1, axis) + fixedNumber(value, 3);
}

} // anonymous namespace

DirectCarvePanel::DirectCarvePanel() : Panel("Direct Carve") {}

DirectCarvePanel::~DirectCarvePanel() = default;

void DirectCarvePanel::setCncController(CncController* cnc) { m_cnc = cnc; }
void DirectCarvePanel::setToolDatabase(ToolDatabase* db) { m_toolDb = db; }
void DirectCarvePanel::setToolboxRepository(ToolboxRepository* repo) { m_toolboxRepo = repo; }
void DirectCarvePanel::setCarveJob(carve::CarveJob* job) { m_carveJob = job; }
void DirectCarvePanel::setFileDialog(FileDialog* dlg) { m_fileDialog = dlg; }
void DirectCarvePanel::setGCodeRepository(GCodeRepository* repo) { m_gcodeRepo = repo; }
void DirectCarvePanel::setGCodePanel(GCodePanel* gcp) { m_gcodePanel = gcp; }
void DirectCarvePanel::setLibraryManager(LibraryManager* library) { m_libraryManager = library; }
void DirectCarvePanel::setMaterialManager(MaterialManager* mgr) { m_materialMgr = mgr; }
void DirectCarvePanel::setProjectManager(ProjectManager* pm) { m_projectManager = pm; }
void DirectCarvePanel::setOpenToolBrowserCallback(std::function<void()> cb) { m_openToolBrowser = std::move(cb); }
void DirectCarvePanel::setOpenMachineProfilesCallback(std::function<void()> cb) {
    m_openMachineProfiles = std::move(cb);
}
void DirectCarvePanel::setCutOptimizerPanel(CutOptimizerPanel* cop) { m_cutOptimizer = cop; }

void DirectCarvePanel::onConnectionChanged(bool connected) { m_cncConnected = connected; }
void DirectCarvePanel::onStatusUpdate(const MachineStatus& status) { m_machineStatus = status; }

void DirectCarvePanel::onRawLine(const std::string& line, bool isSent)
{
    if (isSent || !m_zeroingRunActive) {
        return;
    }

    if (line.rfind("ALARM:", 0) == 0 || line.rfind("error:", 0) == 0) {
        finishZeroingRun(false, "AutoZero stopped: " + line);
        return;
    }

    if (auto result = carve::parseGrblProbeResult(line)) {
        if (!result->contact) {
            finishZeroingRun(false, "AutoZero probe did not make contact.");
            return;
        }
        m_zeroingLastProbeResult = *result;
        m_zeroingSawProbeResult = true;
        return;
    }

    if (line != "ok" || !m_zeroingWaitingForOk) {
        return;
    }

    if (m_zeroingPendingProbeStep != ZeroingStepKind::Command) {
        if (!m_zeroingSawProbeResult || !m_zeroingLastProbeResult.has_value()) {
            finishZeroingRun(false, "AutoZero expected a probe result before ok.");
            return;
        }

        const auto pos = m_zeroingLastProbeResult->position;
        switch (m_zeroingPendingProbeStep) {
        case ZeroingStepKind::ProbeXFirst:
            m_autoZeroXFirst = pos.x;
            break;
        case ZeroingStepKind::ProbeXSecond:
            m_autoZeroXSecond = pos.x;
            break;
        case ZeroingStepKind::ProbeYFirst:
            m_autoZeroYFirst = pos.y;
            break;
        case ZeroingStepKind::ProbeYSecond:
            m_autoZeroYSecond = pos.y;
            break;
        default:
            break;
        }
    }

    m_zeroingWaitingForOk = false;
    m_zeroingPendingProbeStep = ZeroingStepKind::Command;
    m_zeroingSawProbeResult = false;
    m_zeroingLastProbeResult.reset();
    sendNextZeroingStep();
}

void DirectCarvePanel::syncToolpathRapidRateFromProfile()
{
    const auto& profile = Config::instance().getActiveMachineProfile();
    if (profile.rapidRate <= 0.0f) return;

    if (std::abs(m_toolpathConfig.rapidRateMmMin - profile.rapidRate) < 0.001f) {
        return;
    }

    m_toolpathConfig.rapidRateMmMin = profile.rapidRate;
    markToolpathSettingsChanged();
}

void DirectCarvePanel::applyMachineToolpathDefaults(bool updateAppliedKey)
{
    const auto& profile = Config::instance().getActiveMachineProfile();
    bool changed = false;

    auto assignIfValid = [&changed](f32& target, f32 value) {
        if (value <= 0.0f || std::abs(target - value) < 0.001f) {
            return;
        }
        target = value;
        changed = true;
    };

    assignIfValid(m_toolpathConfig.rapidRateMmMin, profile.rapidRate);
    assignIfValid(m_toolpathConfig.feedRateMmMin, profile.defaultFeedRate);
    assignIfValid(m_toolpathConfig.plungeRateMmMin, profile.defaultPlungeRate);
    assignIfValid(m_toolpathConfig.stepdownMm, profile.defaultStepdown);

    m_toolpathConfig.feedRateMmMin =
        std::clamp(m_toolpathConfig.feedRateMmMin, 10.0f, 20000.0f);
    m_toolpathConfig.plungeRateMmMin =
        std::clamp(m_toolpathConfig.plungeRateMmMin, 5.0f, 5000.0f);
    m_toolpathConfig.stepdownMm =
        std::clamp(m_toolpathConfig.stepdownMm, 0.1f, 50.0f);

    if (updateAppliedKey) {
        std::ostringstream key;
        key << profile.name << '|'
            << profile.rapidRate << '|'
            << profile.defaultFeedRate << '|'
            << profile.defaultPlungeRate << '|'
            << profile.defaultStepdown;
        m_machineToolpathDefaultsKey = key.str();
        m_machineToolpathDefaultsApplied = true;
    }

    if (changed) {
        markToolpathSettingsChanged();
    }
}

void DirectCarvePanel::applyMaterialToolpathRecommendation(const MaterialRecord& material)
{
    const auto& finishingTool = m_toolPlan.finishingIntent();
    if (!finishingTool) return;

    CalcInput ci;
    ci.diameter = finishingTool->diameter;
    if (ci.diameter <= 0.0)
        ci.diameter = 3.175; // 1/8" fallback
    ci.num_flutes = finishingTool->num_flutes;
    ci.tool_type = finishingTool->tool_type;
    ci.units = finishingTool->units;
    ci.janka_hardness = static_cast<f64>(material.jankaHardness);
    ci.material_name = material.name;

    const auto& mp = Config::instance().getActiveMachineProfile();
    applyMachineProfileToCalcInput(mp, ci);

    auto result = ToolCalculator::calculate(ci);

    f64 feedMm = result.feed_rate;
    f64 plungeMm = result.plunge_rate;
    f64 stepdownMm = result.stepdown;
    if (ci.units == VtdbUnits::Imperial) {
        feedMm *= 25.4;
        plungeMm *= 25.4;
        stepdownMm *= 25.4;
    }

    m_toolpathConfig.feedRateMmMin =
        std::round(static_cast<f32>(feedMm) / 50.0f) * 50.0f;
    m_toolpathConfig.plungeRateMmMin =
        std::round(static_cast<f32>(plungeMm) / 50.0f) * 50.0f;
    m_toolpathConfig.stepdownMm = static_cast<f32>(stepdownMm);

    m_toolpathConfig.feedRateMmMin =
        std::clamp(m_toolpathConfig.feedRateMmMin, 100.0f, 10000.0f);
    m_toolpathConfig.plungeRateMmMin =
        std::clamp(m_toolpathConfig.plungeRateMmMin, 50.0f, 5000.0f);
    m_toolpathConfig.stepdownMm =
        std::clamp(m_toolpathConfig.stepdownMm, 0.1f, 50.0f);

    markToolpathSettingsChanged();
}

std::string DirectCarvePanel::formatTime(f32 seconds) {
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%dm %ds", mins, secs);
    return buf;
}

std::optional<i64> DirectCarvePanel::selectedMaterialId() const {
    if (m_selectedMaterialIdx < 0 ||
        m_selectedMaterialIdx >= static_cast<int>(m_materialList.size())) {
        return std::nullopt;
    }
    return m_materialList[static_cast<size_t>(m_selectedMaterialIdx)].id;
}

std::string DirectCarvePanel::selectedMaterialName() const {
    if (m_selectedMaterialIdx < 0 ||
        m_selectedMaterialIdx >= static_cast<int>(m_materialList.size())) {
        return m_materialName;
    }
    return m_materialList[static_cast<size_t>(m_selectedMaterialIdx)].name;
}

cnc::SendUnits DirectCarvePanel::detectedSendUnits() const {
    return m_cnc ? m_cnc->sendUnits() : cnc::SendUnits::Millimeters;
}

carve::DirectCarveZeroProbeMode DirectCarvePanel::currentZeroProbeMode() const
{
    switch (m_probeMode) {
    case ProbeMode::XOnly:
        return carve::DirectCarveZeroProbeMode::XOnly;
    case ProbeMode::YOnly:
        return carve::DirectCarveZeroProbeMode::YOnly;
    case ProbeMode::XYCorner:
        return carve::DirectCarveZeroProbeMode::XYCorner;
    case ProbeMode::XYZAuto:
        return carve::DirectCarveZeroProbeMode::XYZAuto;
    case ProbeMode::ZOnly:
        return carve::DirectCarveZeroProbeMode::ZOnly;
    }
    return carve::DirectCarveZeroProbeMode::ZOnly;
}

carve::DirectCarveAutoZeroBitMode
DirectCarvePanel::currentAutoZeroBitMode() const
{
    if (m_autoZeroBitModeManual) {
        return m_autoZeroBitMode;
    }

    const auto& finishingTool = m_toolPlan.finishingIntent();
    const auto& effectiveClearingTool = m_toolPlan.effectiveClearingTool();
    const VtdbToolGeometry* zeroTool = nullptr;
    if (hasCurrentToolpath() && effectiveClearingTool) {
        zeroTool = &*effectiveClearingTool;
    } else if (finishingTool) {
        zeroTool = &*finishingTool;
    }
    if (!zeroTool) {
        return m_autoZeroBitMode;
    }

    switch (zeroTool->tool_type) {
    case VtdbToolType::BallNose:
    case VtdbToolType::TaperedBallNose:
    case VtdbToolType::VBit:
        return carve::DirectCarveAutoZeroBitMode::Tip;
    default:
        return carve::DirectCarveAutoZeroBitMode::Auto;
    }
}

carve::DirectCarveZeroCorner DirectCarvePanel::currentZeroCorner() const
{
    switch (m_probeCorner) {
    case 1:
        return carve::DirectCarveZeroCorner::FrontRight;
    case 2:
        return carve::DirectCarveZeroCorner::BackRight;
    case 3:
        return carve::DirectCarveZeroCorner::BackLeft;
    default:
        return carve::DirectCarveZeroCorner::FrontLeft;
    }
}

carve::DirectCarveZeroingSetup DirectCarvePanel::currentZeroingSetup() const
{
    carve::DirectCarveZeroingSetup setup;
    setup.touchPlate = m_touchPlate;
    setup.probeMode = currentZeroProbeMode();
    setup.bitMode = currentAutoZeroBitMode();
    setup.corner = currentZeroCorner();
    setup.zPlateThicknessMm = m_probeZThickness;
    setup.xyWallThicknessMm = m_probeXYThickness;
    setup.fastProbeMmMin = m_probeFastSpeed;
    setup.slowProbeMmMin = m_probeSlowSpeed;
    setup.searchDistanceMm = m_probeSearchDist;
    setup.retractMm = m_probeRetractDist;
    setup.autoZeroOriginOffsetMm = m_autoZeroOriginOffset;
    setup.autoZeroFinalZRetractMm = m_autoZeroFinalZRetract;
    setup.toolDiameterMm = m_probeToolDiameter.valueMm();
    setup.zeroVerified = m_zeroConfirmed;
    return setup;
}

std::optional<i64> DirectCarvePanel::syncZeroingOpenItem()
{
    const auto operation = pinnedOperationOpenItem();
    if (!operation || !m_preparationPin) {
        return std::nullopt;
    }

    const std::string operationSourceKey = operation->sourceKey.empty()
        ? "project_item:" + std::to_string(operation->id)
        : operation->sourceKey;
    auto item = carve::makeDirectCarveZeroingOpenItem(
        operation->id, operationSourceKey, currentZeroingSetup());
    item.projectId = m_preparationPin->project().value;
    item.parentItemId = m_preparationPin->operationItem().item.value;
    return m_projectManager->upsertOpenItem(std::move(item));
}

bool DirectCarvePanel::validateMachineReady() const {
    return carve::isDirectCarveStepComplete(
        carve::DirectCarveWorkflowStep::Machine, workflowState());
}

// Two-pass probe on a single axis: fast seek, retract, slow accurate pass.
// All commands are incremental (caller must set G91 beforehand).
void DirectCarvePanel::sendProbeAxis(char axis, f32 direction, f32 searchDist,
                                     f32 fastSpeed, f32 slowSpeed, f32 retractDist) {
    char cmd[256];
    f32 sign = (direction >= 0.0f) ? 1.0f : -1.0f;

    // Fast seek
    std::snprintf(cmd, sizeof(cmd), "G38.2 %c%.1f F%.0f",
                  axis, static_cast<double>(sign * searchDist),
                  static_cast<double>(fastSpeed));
    m_cnc->sendCommand(cmd);

    // Retract
    std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                  axis, static_cast<double>(-sign * retractDist));
    m_cnc->sendCommand(cmd);

    // Slow accurate pass
    std::snprintf(cmd, sizeof(cmd), "G38.2 %c%.1f F%.0f",
                  axis, static_cast<double>(sign * (retractDist + 1.0f)),
                  static_cast<double>(slowSpeed));
    m_cnc->sendCommand(cmd);

    // Dwell for position settle
    m_cnc->sendCommand("G4 P0.15");
}

// Probe Z and set WCS Z = plateThickness (zeros Z at stock surface below plate).
void DirectCarvePanel::sendProbeZ(f32 plateThickness) {
    char cmd[256];
    sendProbeAxis('Z', -1.0f, m_probeSearchDist, m_probeFastSpeed,
                  m_probeSlowSpeed, m_probeRetractDist);

    std::snprintf(cmd, sizeof(cmd), "G10 L20 P0 Z%.3f",
                  static_cast<double>(plateThickness));
    m_cnc->sendCommand(cmd);

    // Retract above plate
    std::snprintf(cmd, sizeof(cmd), "G0 Z%.1f",
                  static_cast<double>(m_probeRetractDist));
    m_cnc->sendCommand(cmd);
}

// Probe an XY axis, set WCS offset accounting for plate wall + tool radius.
void DirectCarvePanel::sendProbeXY(char axis, f32 direction,
                                   f32 xyThickness, f32 toolRadius) {
    char cmd[256];
    f32 sign = (direction >= 0.0f) ? 1.0f : -1.0f;
    sendProbeAxis(axis, direction, m_probeSearchDist, m_probeFastSpeed,
                  m_probeSlowSpeed, m_probeRetractDist);

    // Set axis = -(xyThickness + toolRadius) * sign
    // Contact point is at the plate outer face; work zero is at the
    // stock edge (inset by wall thickness + tool radius from contact).
    f32 offset = -sign * (xyThickness + toolRadius);
    std::snprintf(cmd, sizeof(cmd), "G10 L20 P0 %c%.3f",
                  axis, static_cast<double>(offset));
    m_cnc->sendCommand(cmd);

    // Retract away from edge
    std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                  axis, static_cast<double>(-sign * m_probeRetractDist));
    m_cnc->sendCommand(cmd);
}

void DirectCarvePanel::startSienciAutoZeroProbe()
{
    if (!m_cnc || !m_cncConnected) {
        m_zeroingRunMessage = "AutoZero requires a connected controller.";
        return;
    }

    auto profile = carve::defaultSienciAutoZeroProfile();
    bool needsZ = (m_probeMode == ProbeMode::ZOnly ||
                   m_probeMode == ProbeMode::XYZAuto);
    bool needsX = (m_probeMode == ProbeMode::XOnly ||
                   m_probeMode == ProbeMode::XYCorner ||
                   m_probeMode == ProbeMode::XYZAuto);
    bool needsY = (m_probeMode == ProbeMode::YOnly ||
                   m_probeMode == ProbeMode::XYCorner ||
                   m_probeMode == ProbeMode::XYZAuto);
    bool needsXY = needsX || needsY;

    auto addCommand = [this](const std::string& command) {
        m_zeroingSteps.push_back({ZeroingStepKind::Command, command});
    };
    auto addStep = [this](ZeroingStepKind kind, const std::string& command = {}) {
        m_zeroingSteps.push_back({kind, command});
    };

    m_zeroingSteps.clear();
    m_zeroingStepIndex = 0;
    m_zeroingPendingProbeStep = ZeroingStepKind::Command;
    m_zeroingWaitingForOk = false;
    m_zeroingSawProbeResult = false;
    m_zeroingLastProbeResult.reset();
    m_autoZeroXFirst = 0.0f;
    m_autoZeroXSecond = 0.0f;
    m_autoZeroYFirst = 0.0f;
    m_autoZeroYSecond = 0.0f;
    m_zeroingRunActive = true;
    m_zeroingRunMessage = "AutoZero starting.";

    f32 zSearch = std::max(1.0f, m_probeSearchDist);
    f32 lateralSearch = std::max(1.0f, m_probeSearchDist);
    f32 retract = std::clamp(m_probeRetractDist, 0.1f, 20.0f);
    f32 slowProbeTravel = retract + 1.0f;
    f32 zLiftForXY = std::max(3.0f, retract + 1.0f);
    f32 zThickness = std::max(0.0f, m_probeZThickness);
    f32 finalZ = std::max(1.0f, m_autoZeroFinalZRetract);

    addCommand("G21");
    addCommand("G91");

    if (needsZ || needsXY) {
        addCommand("G38.2 Z-" + fixedNumber(zSearch, 3) +
                   " F" + feedNumber(m_probeFastSpeed));
        addCommand("G0 Z" + fixedNumber(retract, 3));
        addCommand("G38.2 Z-" + fixedNumber(slowProbeTravel, 3) +
                   " F" + feedNumber(m_probeSlowSpeed));
        if (needsZ) {
            addCommand("G10 L20 P0 Z" + fixedNumber(zThickness, 3));
        }
        addCommand("G0 Z" + fixedNumber(needsXY ? zLiftForXY : finalZ, 3));
    }

    auto bitMode = currentAutoZeroBitMode();
    f32 approach = bitMode == carve::DirectCarveAutoZeroBitMode::Tip
                       ? profile.tipModeApproachMm
                       : profile.autoModeApproachMm;
    f32 span = bitMode == carve::DirectCarveAutoZeroBitMode::Tip
                   ? profile.tipModeSpanMm
                   : profile.autoModeSpanMm;

    auto addAxisCentering = [&](char axis,
                                ZeroingStepKind first,
                                ZeroingStepKind second,
                                ZeroingStepKind moveToCenter,
                                ZeroingStepKind setOffset) {
        addCommand("G0 " + axisWord(axis, -approach));
        addCommand("G38.2 " + axisWord(axis, -lateralSearch) +
                   " F" + feedNumber(m_probeFastSpeed));
        addCommand("G0 " + axisWord(axis, retract));
        addStep(first, "G38.2 " + axisWord(axis, -slowProbeTravel) +
                       " F" + feedNumber(m_probeSlowSpeed));
        addCommand("G0 " + axisWord(axis, span));
        addCommand("G38.2 " + axisWord(axis, lateralSearch) +
                   " F" + feedNumber(m_probeFastSpeed));
        addCommand("G0 " + axisWord(axis, -retract));
        addStep(second, "G38.2 " + axisWord(axis, slowProbeTravel) +
                        " F" + feedNumber(m_probeSlowSpeed));
        addStep(moveToCenter);
        addStep(setOffset);
    };

    if (needsX) {
        addAxisCentering('X',
                         ZeroingStepKind::ProbeXFirst,
                         ZeroingStepKind::ProbeXSecond,
                         ZeroingStepKind::MoveToXCenter,
                         ZeroingStepKind::SetXOffset);
    }
    if (needsY) {
        addAxisCentering('Y',
                         ZeroingStepKind::ProbeYFirst,
                         ZeroingStepKind::ProbeYSecond,
                         ZeroingStepKind::MoveToYCenter,
                         ZeroingStepKind::SetYOffset);
    }

    if (needsXY) {
        addCommand("G90");
        addCommand("G0 X0 Y0");
        if (needsZ) {
            addCommand("G0 Z" + fixedNumber(finalZ, 3));
        }
    } else {
        addCommand("G90");
    }

    sendNextZeroingStep();
}

void DirectCarvePanel::sendNextZeroingStep()
{
    if (!m_zeroingRunActive || !m_cnc) {
        return;
    }

    if (m_zeroingStepIndex >= m_zeroingSteps.size()) {
        finishZeroingRun(true,
                         "AutoZero probe sequence completed. Confirm zero before carving.");
        return;
    }

    auto step = m_zeroingSteps[m_zeroingStepIndex++];
    std::string command = step.command;
    char buf[160];

    switch (step.kind) {
    case ZeroingStepKind::MoveToXCenter: {
        f32 delta = (m_autoZeroXFirst - m_autoZeroXSecond) * 0.5f;
        std::snprintf(buf, sizeof(buf), "G0 X%.3f", static_cast<double>(delta));
        command = buf;
        break;
    }
    case ZeroingStepKind::SetXOffset: {
        f32 offset = (m_probeCorner == 1 || m_probeCorner == 2)
                         ? -m_autoZeroOriginOffset
                         : m_autoZeroOriginOffset;
        std::snprintf(buf, sizeof(buf), "G10 L20 P0 X%.3f",
                      static_cast<double>(offset));
        command = buf;
        break;
    }
    case ZeroingStepKind::MoveToYCenter: {
        f32 delta = (m_autoZeroYFirst - m_autoZeroYSecond) * 0.5f;
        std::snprintf(buf, sizeof(buf), "G0 Y%.3f", static_cast<double>(delta));
        command = buf;
        break;
    }
    case ZeroingStepKind::SetYOffset: {
        f32 offset = (m_probeCorner == 2 || m_probeCorner == 3)
                         ? -m_autoZeroOriginOffset
                         : m_autoZeroOriginOffset;
        std::snprintf(buf, sizeof(buf), "G10 L20 P0 Y%.3f",
                      static_cast<double>(offset));
        command = buf;
        break;
    }
    default:
        break;
    }

    if (command.empty()) {
        finishZeroingRun(false, "AutoZero generated an empty probe step.");
        return;
    }

    m_zeroingPendingProbeStep =
        (step.kind == ZeroingStepKind::ProbeXFirst ||
         step.kind == ZeroingStepKind::ProbeXSecond ||
         step.kind == ZeroingStepKind::ProbeYFirst ||
         step.kind == ZeroingStepKind::ProbeYSecond)
            ? step.kind
            : ZeroingStepKind::Command;
    m_zeroingWaitingForOk = true;
    m_zeroingSawProbeResult = false;
    m_zeroingLastProbeResult.reset();
    m_zeroingRunMessage = "AutoZero: " + command;
    m_cnc->sendCommand(command);
}

void DirectCarvePanel::finishZeroingRun(bool success, const std::string& message)
{
    m_zeroingRunActive = false;
    m_zeroingWaitingForOk = false;
    m_zeroingPendingProbeStep = ZeroingStepKind::Command;
    m_zeroingSawProbeResult = false;
    m_zeroingLastProbeResult.reset();
    m_zeroingSteps.clear();
    m_zeroingStepIndex = 0;
    m_zeroingRunMessage = message;

    if (success) {
        clearFinalConfirmation();
        (void)syncOperationOpenItem();
    } else if (m_cnc && m_cncConnected) {
        m_cnc->sendCommand("G90");
    }
}

} // namespace dw
