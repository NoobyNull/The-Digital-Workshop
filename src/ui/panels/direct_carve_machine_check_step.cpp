// Machine-readiness view for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {

constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kRed = colors::kError;
constexpr auto& kYellow = colors::kWarning;

void statusBullet(bool ok, const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ok ? kGreen : kRed);
    ImGui::BulletText("%s %s", ok ? "OK" : "FAIL", label);
    ImGui::PopStyleColor();
}

u32 activeLimitPins(const MachineStatus& status) {
    return status.inputPins &
        (cnc::PIN_X_LIMIT | cnc::PIN_Y_LIMIT | cnc::PIN_Z_LIMIT);
}

std::string activeLimitPinLabel(u32 pins) {
    std::string label;
    if ((pins & cnc::PIN_X_LIMIT) != 0) label += "X";
    if ((pins & cnc::PIN_Y_LIMIT) != 0) {
        if (!label.empty()) label += " ";
        label += "Y";
    }
    if ((pins & cnc::PIN_Z_LIMIT) != 0) {
        if (!label.empty()) label += " ";
        label += "Z";
    }
    return label;
}

} // namespace

void DirectCarvePanel::renderMachineCheck() {
    ImGui::TextUnformatted("Machine Setup — Connection & Homing");
    ImGui::TextWrapped(
        "First confirm the controller, homing, limits, and safe retract height.");
    ImGui::Spacing();

    bool connected = m_cncConnected;
    bool idle = (m_machineStatus.state == MachineState::Idle);
    bool notAlarm = (m_machineStatus.state != MachineState::Alarm &&
                     m_machineStatus.state != MachineState::Unknown);
    const u32 limitPins = activeLimitPins(m_machineStatus);
    const bool limitSwitchesClear = limitPins == 0;
    auto& cfg = Config::instance();
    const auto& profile = cfg.getActiveMachineProfile();
    bool profileOk = (profile.maxTravelX > 0.0f && profile.maxTravelY > 0.0f &&
                      profile.maxTravelZ > 0.0f);

    // Show detected machine
    if (connected && profileOk) {
        ImGui::Text("Machine: %s", profile.name.c_str());
    } else if (connected) {
        ImGui::TextColored(kYellow, "Machine: Connected (no profile configured)");
    } else {
        ImGui::TextColored(kRed, "Machine: Not connected");
    }
    ImGui::Spacing();

    // Checklist
    statusBullet(connected, "CNC connected");
    statusBullet(notAlarm, "No alarm");
    statusBullet(idle, "Machine idle");
    statusBullet(profileOk, "Machine profile configured");
    statusBullet(m_homingVerified || m_homingSkipped,
                 "Machine homed or explicitly skipped");
    statusBullet(limitSwitchesClear, "Limit switches clear");
    if (!limitSwitchesClear) {
        const auto axes = activeLimitPinLabel(limitPins);
        ImGui::TextColored(kRed, "Active limit input(s): %s", axes.c_str());
    }
    statusBullet(m_safeZConfirmed, "Safe Z verified");
    ImGui::Spacing();

    bool canSend = (m_cnc != nullptr && connected);
    float fs = ImGui::GetFontSize();
    float bw = fs * 10.0f;

    // Homing
    if (!canSend) ImGui::BeginDisabled();
    if (ImGui::Button("Home Machine", ImVec2(bw, 0))) {
        m_cnc->sendCommand("$H");
        m_homingVerified = false;
        m_homingSkipped = false;
        clearFinalConfirmation();
    }
    if (!canSend) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("Machine has been homed", &m_homingVerified)) {
        if (m_homingVerified) m_homingSkipped = false;
        clearFinalConfirmation();
    }
    if (ImGui::Checkbox("Skip homing; position is already known",
                        &m_homingSkipped)) {
        if (m_homingSkipped) m_homingVerified = false;
        clearFinalConfirmation();
    }

    // Safe Z test
    ImGui::Spacing();
    if (connected && idle) {
        if (ImGui::Button("Test Safe Z", ImVec2(bw, 0))) {
            if (m_cnc) {
                char cmd[64];
                std::snprintf(cmd, sizeof(cmd), "G0 Z%.3f",
                              static_cast<double>(m_toolpathConfig.safeZMm));
                m_cnc->sendCommand(cmd);
                m_safeZConfirmed = true;
                clearFinalConfirmation();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip (confirm safe Z)", ImVec2(bw * 1.2f, 0))) {
            m_safeZConfirmed = true;
            clearFinalConfirmation();
        }
    }

    if (m_machineStatus.state == MachineState::Alarm) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("Machine is in ALARM state. Unlock or reset before continuing.");
        ImGui::PopStyleColor();
        if (canSend) {
            if (ImGui::Button("Unlock ($X)", ImVec2(bw, 0)))
                m_cnc->sendCommand("$X");
        }
    }
}

} // namespace dw
