// Work-zero confirmation and probing view for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "core/carve/carve_job.h"
#include "core/cnc/cnc_controller.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {

constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kYellow = colors::kWarning;

} // namespace

void DirectCarvePanel::renderZeroConfirm() {
    ImGui::TextUnformatted("Machine Setup — Work Zero");
    ImGui::TextWrapped("Next set the work origin that the previewed toolpath will use.");
    ImGui::Spacing();

    const auto& finishingTool = m_toolPlan.finishingIntent();
    const auto& effectiveClearingTool = m_toolPlan.effectiveClearingTool();
    const VtdbToolGeometry* zeroTool = hasCurrentToolpath() && effectiveClearingTool
                                           ? &*effectiveClearingTool
                                           : (finishingTool ? &*finishingTool : nullptr);
    if (zeroTool) {
        ImGui::TextDisabled("Initial tool: %s", resolveToolNameFormat(*zeroTool).c_str());
        if (hasCurrentToolpath() && m_carveJob->toolpath().requiresToolChange) {
            ImGui::TextColored(kYellow,
                               "Roughing runs first; re-zero Z after the tool-change pause.");
        }
        ImGui::Spacing();
    }

    ImGui::Text("Current Work Position:");
    ImGui::Indent();
    ImGui::Text("X: %.3f  Y: %.3f  Z: %.3f",
                static_cast<double>(m_machineStatus.workPos.x),
                static_cast<double>(m_machineStatus.workPos.y),
                static_cast<double>(m_machineStatus.workPos.z));
    ImGui::Unindent();

    bool nearZero = (std::fabs(m_machineStatus.workPos.x) < 0.5f &&
                     std::fabs(m_machineStatus.workPos.y) < 0.5f &&
                     std::fabs(m_machineStatus.workPos.z) < 0.5f);
    if (nearZero)
        ImGui::TextColored(kGreen, "Position is near zero origin.");

    ImGui::Spacing();

    bool canSend = (m_cnc != nullptr && m_cncConnected);
    bool isIdle = canSend && m_machineStatus.state == MachineState::Idle;

    // --- Manual Zero ---
    if (ImGui::CollapsingHeader("Manual Zero", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextWrapped("Position the tool at work zero "
                           "(bottom-left of stock, Z on top surface).");
        ImGui::Spacing();

        float bw = ImGui::GetFontSize() * 10.0f;
        if (!canSend)
            ImGui::BeginDisabled();
        if (ImGui::Button("Set Zero Here", ImVec2(bw, 0)))
            m_cnc->sendCommand("G10 L20 P0 X0 Y0 Z0");
        ImGui::SameLine();
        if (ImGui::Button("Zero XY Only", ImVec2(bw, 0)))
            m_cnc->sendCommand("G10 L20 P0 X0 Y0");
        ImGui::SameLine();
        if (ImGui::Button("Zero Z Only", ImVec2(bw, 0)))
            m_cnc->sendCommand("G10 L20 P0 Z0");
        if (!canSend)
            ImGui::EndDisabled();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // --- Touch Plate Probe ---
    if (ImGui::CollapsingHeader("Touch Plate Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        bool zeroingConfigChanged = false;

        // Probe pin indicator
        bool probeActive = (m_machineStatus.inputPins & cnc::PIN_PROBE) != 0;
        ImVec4 probeColor = probeActive ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                        : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(probeColor,
                           "Probe pin: %s",
                           probeActive ? "ACTIVE (circuit closed)" : "inactive");
        ImGui::Spacing();

        const char* plateLabels[] = {"Generic touch plate", "Sienci AutoZero"};
        int plateIdx = m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero ? 1 : 0;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (ImGui::Combo("Touch Plate", &plateIdx, plateLabels, 2)) {
            m_touchPlate = plateIdx == 1 ? carve::DirectCarveTouchPlate::SienciAutoZero
                                         : carve::DirectCarveTouchPlate::Generic;
            if (m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero) {
                auto profile = carve::defaultSienciAutoZeroProfile();
                if (std::fabs(m_probeZThickness - 15.0f) < 0.001f) {
                    m_probeZThickness = profile.zPlateThicknessMm;
                }
                m_autoZeroOriginOffset = profile.originOffsetMm;
                m_autoZeroFinalZRetract = profile.finalZRetractMm;
                m_probeSearchDist = profile.lateralSearchMm;
            }
            zeroingConfigChanged = true;
        }
        if (m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero) {
            const char* bitModeLabels[] = {"Auto (straight bits)", "Tip (V, ball, tapered)"};
            int bitModeIdx = currentAutoZeroBitMode() == carve::DirectCarveAutoZeroBitMode::Tip ? 1
                                                                                                : 0;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            if (ImGui::Combo("Bit Geometry", &bitModeIdx, bitModeLabels, 2)) {
                m_autoZeroBitMode = bitModeIdx == 1 ? carve::DirectCarveAutoZeroBitMode::Tip
                                                    : carve::DirectCarveAutoZeroBitMode::Auto;
                m_autoZeroBitModeManual = true;
                zeroingConfigChanged = true;
            }
            ImGui::TextDisabled("Z-only uses back lip; XY/XYZ starts over inner square.");
        }
        ImGui::Spacing();

        // --- Mode selector ---
        const char* modeLabels[] = {"Z Only", "X Only", "Y Only", "XY Corner", "XYZ Auto"};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        int modeInt = static_cast<int>(m_probeMode);
        if (ImGui::Combo("Probe Mode", &modeInt, modeLabels, 5)) {
            m_probeMode = static_cast<ProbeMode>(modeInt);
            zeroingConfigChanged = true;
        }

        bool needsXY = (m_probeMode == ProbeMode::XOnly || m_probeMode == ProbeMode::YOnly ||
                        m_probeMode == ProbeMode::XYCorner || m_probeMode == ProbeMode::XYZAuto);

        // Corner direction (for XY modes)
        if (needsXY) {
            const char* cornerLabels[] = {"Bottom-Left (probe -X, -Y)",
                                          "Bottom-Right (probe +X, -Y)",
                                          "Top-Right (probe +X, +Y)",
                                          "Top-Left (probe -X, +Y)"};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            if (ImGui::Combo("Probe Direction", &m_probeCorner, cornerLabels, 4)) {
                zeroingConfigChanged = true;
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Parameters");

        float fieldW = ImGui::GetFontSize() * 8.0f;

        // Z thickness (for Z and XYZ modes)
        bool needsZ = (m_probeMode == ProbeMode::ZOnly || m_probeMode == ProbeMode::XYZAuto);
        if (needsZ) {
            ImGui::SetNextItemWidth(fieldW);
            zeroingConfigChanged |=
                ImGui::InputFloat("Z Plate Thickness (mm)", &m_probeZThickness, 0.5f, 1.0f, "%.2f");
            m_probeZThickness = std::max(0.0f, m_probeZThickness);
        }

        // XY compensation geometry
        if (needsXY) {
            if (m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero) {
                ImGui::SetNextItemWidth(fieldW);
                zeroingConfigChanged |= ImGui::InputFloat(
                    "Origin Offset (mm)", &m_autoZeroOriginOffset, 0.1f, 1.0f, "%.3f");
                m_autoZeroOriginOffset = std::max(0.0f, m_autoZeroOriginOffset);
            } else {
                ImGui::SetNextItemWidth(fieldW);
                zeroingConfigChanged |= ImGui::InputFloat(
                    "XY Wall Thickness (mm)", &m_probeXYThickness, 0.5f, 1.0f, "%.2f");
                m_probeXYThickness = std::max(0.0f, m_probeXYThickness);
            }
        }

        // Generic XY compensation follows whichever tool runs first. A manual
        // override is retained until the operator explicitly returns to auto.
        if (needsXY && m_touchPlate == carve::DirectCarveTouchPlate::Generic) {
            zeroingConfigChanged |= m_probeToolDiameter.refreshAutomatic(zeroTool);

            f32 probeToolDiameter = m_probeToolDiameter.valueMm();
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::InputFloat("Tool Diameter (mm)", &probeToolDiameter, 0.1f, 1.0f, "%.3f")) {
                m_probeToolDiameter.setManualValue(probeToolDiameter);
                zeroingConfigChanged = true;
            }
            if (m_probeToolDiameter.manualOverride()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(manual override)");
                if (zeroTool) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Use initial tool")) {
                        zeroingConfigChanged |= m_probeToolDiameter.resumeAutomatic(zeroTool);
                    }
                }
            } else if (zeroTool) {
                ImGui::SameLine();
                ImGui::TextDisabled("(from %s)", resolveToolNameFormat(*zeroTool).c_str());
            }
        } else if (needsXY) {
            ImGui::TextDisabled(
                "AutoZero measures center from probe contacts; tool diameter is not assumed.");
        }

        ImGui::SetNextItemWidth(fieldW);
        zeroingConfigChanged |=
            ImGui::InputFloat("Fast Speed (mm/min)", &m_probeFastSpeed, 10, 50, "%.0f");
        m_probeFastSpeed = std::clamp(m_probeFastSpeed, 10.0f, 1000.0f);

        ImGui::SetNextItemWidth(fieldW);
        zeroingConfigChanged |=
            ImGui::InputFloat("Slow Speed (mm/min)", &m_probeSlowSpeed, 5, 25, "%.0f");
        m_probeSlowSpeed = std::clamp(m_probeSlowSpeed, 5.0f, 500.0f);

        ImGui::SetNextItemWidth(fieldW);
        zeroingConfigChanged |=
            ImGui::InputFloat("Search Distance (mm)", &m_probeSearchDist, 5, 10, "%.1f");
        m_probeSearchDist = std::clamp(m_probeSearchDist, 1.0f, 200.0f);

        ImGui::SetNextItemWidth(fieldW);
        zeroingConfigChanged |=
            ImGui::InputFloat("Retract (mm)", &m_probeRetractDist, 0.5f, 1, "%.1f");
        m_probeRetractDist = std::clamp(m_probeRetractDist, 0.1f, 20.0f);

        if (m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero) {
            ImGui::SetNextItemWidth(fieldW);
            zeroingConfigChanged |= ImGui::InputFloat(
                "Final Z Retract (mm)", &m_autoZeroFinalZRetract, 0.1f, 1.0f, "%.2f");
            m_autoZeroFinalZRetract = std::clamp(m_autoZeroFinalZRetract, 1.0f, 50.0f);
        }

        ImGui::Spacing();

        // Direction signs based on corner
        // BL=0: probe toward -X,-Y  BR=1: +X,-Y  TR=2: +X,+Y  TL=3: -X,+Y
        f32 xDir = (m_probeCorner == 0 || m_probeCorner == 3) ? -1.0f : 1.0f;
        f32 yDir = (m_probeCorner == 0 || m_probeCorner == 1) ? -1.0f : 1.0f;
        f32 toolR = m_probeToolDiameter.valueMm() * 0.5f;

        // --- Command preview ---
        ImGui::SeparatorText("Probe sequence");

        ImGui::TextDisabled("G21 G91  (metric, incremental)");

        if (m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero) {
            ImGui::TextDisabled("Runs one command at a time and reads [PRB] contacts.");
            if (needsZ) {
                ImGui::TextDisabled("Z: two-pass probe, then G10 L20 P0 Z%.3f",
                                    static_cast<double>(m_probeZThickness));
            }
            if (needsXY) {
                f32 xOffset = (m_probeCorner == 1 || m_probeCorner == 2) ? -m_autoZeroOriginOffset
                                                                         : m_autoZeroOriginOffset;
                f32 yOffset = (m_probeCorner == 2 || m_probeCorner == 3) ? -m_autoZeroOriginOffset
                                                                         : m_autoZeroOriginOffset;
                ImGui::TextDisabled("XY: probe both sides, move to measured center.");
                ImGui::TextDisabled("G10 L20 P0 X%.3f Y%.3f",
                                    static_cast<double>(xOffset),
                                    static_cast<double>(yOffset));
            }
        } else {
            switch (m_probeMode) {
            case ProbeMode::ZOnly:
                ImGui::TextDisabled("G38.2 Z-%.1f F%.0f  (fast)",
                                    static_cast<double>(m_probeSearchDist),
                                    static_cast<double>(m_probeFastSpeed));
                ImGui::TextDisabled("G0 Z%.1f  (retract)", static_cast<double>(m_probeRetractDist));
                ImGui::TextDisabled("G38.2 Z-%.1f F%.0f  (slow)",
                                    static_cast<double>(m_probeRetractDist + 1.0f),
                                    static_cast<double>(m_probeSlowSpeed));
                ImGui::TextDisabled("G10 L20 P0 Z%.2f", static_cast<double>(m_probeZThickness));
                break;

            case ProbeMode::XOnly:
                ImGui::TextDisabled("G38.2 X%.1f F%.0f  (fast)",
                                    static_cast<double>(xDir * m_probeSearchDist),
                                    static_cast<double>(m_probeFastSpeed));
                ImGui::TextDisabled("G10 L20 P0 X%.3f  (wall + tool radius)",
                                    static_cast<double>(-xDir * (m_probeXYThickness + toolR)));
                break;

            case ProbeMode::YOnly:
                ImGui::TextDisabled("G38.2 Y%.1f F%.0f  (fast)",
                                    static_cast<double>(yDir * m_probeSearchDist),
                                    static_cast<double>(m_probeFastSpeed));
                ImGui::TextDisabled("G10 L20 P0 Y%.3f  (wall + tool radius)",
                                    static_cast<double>(-yDir * (m_probeXYThickness + toolR)));
                break;

            case ProbeMode::XYCorner:
                ImGui::TextDisabled("Probe X -> set X offset");
                ImGui::TextDisabled("Probe Y -> set Y offset");
                ImGui::TextDisabled("Compensation: wall(%.1f) + radius(%.2f) = %.2f mm",
                                    static_cast<double>(m_probeXYThickness),
                                    static_cast<double>(toolR),
                                    static_cast<double>(m_probeXYThickness + toolR));
                break;

            case ProbeMode::XYZAuto:
                ImGui::TextDisabled("1. Probe Z on top of block -> set Z");
                ImGui::TextDisabled("2. Move past X edge, drop Z, probe X -> set X");
                ImGui::TextDisabled("3. Move past Y edge, probe Y -> set Y");
                ImGui::TextDisabled("4. Return to work zero");
                ImGui::TextDisabled("Compensation: wall(%.1f) + radius(%.2f) = %.2f mm",
                                    static_cast<double>(m_probeXYThickness),
                                    static_cast<double>(toolR),
                                    static_cast<double>(m_probeXYThickness + toolR));
                break;
            }
        }

        ImGui::TextDisabled("G90  (restore absolute)");

        ImGui::Spacing();

        // --- Run Probe ---
        bool canProbe = isIdle && !m_zeroingRunActive;
        if (!canProbe)
            ImGui::BeginDisabled();

        const char* probeLabel = m_zeroingRunActive ? "Probing..." : "Run Probe";
        float probeW = ImGui::CalcTextSize(probeLabel).x + ImGui::GetStyle().FramePadding.x * 4;
        float probeH = ImGui::GetFrameHeight() * 1.5f;

        if (ImGui::Button(probeLabel, ImVec2(probeW, probeH))) {
            if (m_touchPlate == carve::DirectCarveTouchPlate::SienciAutoZero) {
                startSienciAutoZeroProbe();
            } else {
                char cmd[256];
                m_cnc->sendCommand("G21 G91");

                switch (m_probeMode) {
                case ProbeMode::ZOnly:
                    sendProbeZ(m_probeZThickness);
                    break;

                case ProbeMode::XOnly:
                    sendProbeXY('X', xDir, m_probeXYThickness, toolR);
                    break;

                case ProbeMode::YOnly:
                    sendProbeXY('Y', yDir, m_probeXYThickness, toolR);
                    break;

                case ProbeMode::XYCorner:
                    sendProbeXY('X', xDir, m_probeXYThickness, toolR);
                    sendProbeXY('Y', yDir, m_probeXYThickness, toolR);
                    break;

                case ProbeMode::XYZAuto: {
                    sendProbeZ(m_probeZThickness);

                    f32 clearance = m_probeXYThickness + m_probeRetractDist + toolR + 6.0f;
                    std::snprintf(
                        cmd, sizeof(cmd), "G0 %c%.1f", 'X', static_cast<double>(-xDir * clearance));
                    m_cnc->sendCommand(cmd);

                    f32 zDrop = m_probeZThickness + m_probeRetractDist + 2.0f;
                    std::snprintf(cmd, sizeof(cmd), "G0 Z-%.1f", static_cast<double>(zDrop));
                    m_cnc->sendCommand(cmd);

                    sendProbeXY('X', xDir, m_probeXYThickness, toolR);

                    std::snprintf(
                        cmd, sizeof(cmd), "G0 %c%.1f", 'Y', static_cast<double>(-yDir * clearance));
                    m_cnc->sendCommand(cmd);
                    std::snprintf(
                        cmd, sizeof(cmd), "G0 %c%.1f", 'X', static_cast<double>(xDir * clearance));
                    m_cnc->sendCommand(cmd);

                    sendProbeXY('Y', yDir, m_probeXYThickness, toolR);

                    std::snprintf(cmd,
                                  sizeof(cmd),
                                  "G0 Z%.1f",
                                  static_cast<double>(zDrop + m_probeRetractDist));
                    m_cnc->sendCommand(cmd);

                    m_cnc->sendCommand("G90");
                    m_cnc->sendCommand("G0 X0 Y0");
                    m_cnc->sendCommand("G91");
                    break;
                }
                }

                m_cnc->sendCommand("G90");
                (void)syncOperationOpenItem();
            }
        }
        if (!canProbe)
            ImGui::EndDisabled();

        if (!isIdle && canSend) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Machine must be Idle to probe");
        }
        if (!m_zeroingRunMessage.empty()) {
            ImGui::TextWrapped("%s", m_zeroingRunMessage.c_str());
        }
        if (zeroingConfigChanged) {
            setPreparationDirty(true);
            (void)syncOperationOpenItem();
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();
    if (ImGui::Checkbox("Work zero has been set and verified", &m_zeroConfirmed)) {
        clearFinalConfirmation();
        setPreparationDirty(true);
        (void)syncOperationOpenItem();
    }
}

} // namespace dw
