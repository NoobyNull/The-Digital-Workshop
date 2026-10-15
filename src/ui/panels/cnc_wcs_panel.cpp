#include "ui/panels/cnc_wcs_panel.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "ui/icons.h"
#include "ui/ui_colors.h"

namespace dw {

CncWcsPanel::CncWcsPanel() : Panel("WCS") {}

void CncWcsPanel::render() {
    if (!m_open)
        return;

    applyMinSize(16, 8);
    if (!ImGui::Begin(m_title.c_str(), closeHandle())) {
        ImGui::End();
        return;
    }

    if (!m_connected) {
        ImGui::Spacing();
        ImGui::TextColored(colors::kDimmed, "%s Disconnected", Icons::Unlink);
        ImGui::TextDisabled("Connect a CNC machine to set work zero");
        ImGui::End();
        return;
    }

    renderZeroButtons();
    ImGui::Spacing();
    renderWcsSelector();
    ImGui::Spacing();
    renderOffsetDisplay();
    renderConfirmationPopup();

    ImGui::End();
}

void CncWcsPanel::renderZeroButtons() {
    ImGui::SeparatorText("Set Work Zero");

    // Show current work position for reference
    ImGui::TextDisabled("Current work position:");
    ImGui::Text("X: %+.3f  Y: %+.3f  Z: %+.3f",
                static_cast<double>(m_status.workPos.x),
                static_cast<double>(m_status.workPos.y),
                static_cast<double>(m_status.workPos.z));
    ImGui::Spacing();

    bool canZero = m_cnc && m_status.state == MachineState::Idle;
    if (!canZero)
        ImGui::BeginDisabled();

    int pNum = m_activeWcs + 1; // P1=G54, P2=G55, etc.

    // Zero X
    float zeroBtnW = ImGui::CalcTextSize("Zero X").x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button("Zero X", ImVec2(zeroBtnW, 0))) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "G10 L20 P%d X0", pNum);
        m_confirmZeroCmd = cmd;
        char label[128];
        std::snprintf(label, sizeof(label),
                      "Set X work zero to current position in %s?\nCurrent X: %+.3f",
                      WCS_NAMES[m_activeWcs], static_cast<double>(m_status.workPos.x));
        m_confirmZeroLabel = label;
        m_confirmZeroOpen = true;
    }
    ImGui::SameLine();

    // Zero Y
    if (ImGui::Button("Zero Y", ImVec2(zeroBtnW, 0))) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "G10 L20 P%d Y0", pNum);
        m_confirmZeroCmd = cmd;
        char label[128];
        std::snprintf(label, sizeof(label),
                      "Set Y work zero to current position in %s?\nCurrent Y: %+.3f",
                      WCS_NAMES[m_activeWcs], static_cast<double>(m_status.workPos.y));
        m_confirmZeroLabel = label;
        m_confirmZeroOpen = true;
    }
    ImGui::SameLine();

    // Zero Z
    if (ImGui::Button("Zero Z", ImVec2(zeroBtnW, 0))) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "G10 L20 P%d Z0", pNum);
        m_confirmZeroCmd = cmd;
        char label[128];
        std::snprintf(label, sizeof(label),
                      "Set Z work zero to current position in %s?\nCurrent Z: %+.3f",
                      WCS_NAMES[m_activeWcs], static_cast<double>(m_status.workPos.z));
        m_confirmZeroLabel = label;
        m_confirmZeroOpen = true;
    }
    ImGui::SameLine();

    // Zero All — slightly wider, warning color
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
    float zeroAllBtnW = ImGui::CalcTextSize("Zero All").x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button("Zero All", ImVec2(zeroAllBtnW, 0))) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "G10 L20 P%d X0 Y0 Z0", pNum);
        m_confirmZeroCmd = cmd;
        char label[256];
        std::snprintf(label, sizeof(label),
                      "Set ALL axes to work zero in %s?\n\n"
                      "Current position:\n  X: %+.3f\n  Y: %+.3f\n  Z: %+.3f",
                      WCS_NAMES[m_activeWcs],
                      static_cast<double>(m_status.workPos.x),
                      static_cast<double>(m_status.workPos.y),
                      static_cast<double>(m_status.workPos.z));
        m_confirmZeroLabel = label;
        m_confirmZeroOpen = true;
    }
    ImGui::PopStyleColor(2);

    if (!canZero)
        ImGui::EndDisabled();
}

void CncWcsPanel::renderWcsSelector() {
    ImGui::SeparatorText("Coordinate System");

    bool canSwitch = m_cnc && !m_cnc->isStreaming();
    if (!canSwitch)
        ImGui::BeginDisabled();

    for (int i = 0; i < NUM_WCS; ++i) {
        if (i > 0)
            ImGui::SameLine();

        bool isActive = (i == m_activeWcs);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        }

        float wcsBtnW = ImGui::CalcTextSize("G59").x + ImGui::GetStyle().FramePadding.x * 4;
        if (ImGui::Button(WCS_NAMES[i], ImVec2(wcsBtnW, 0))) {
            if (m_cnc) {
                m_cnc->sendCommand(WCS_NAMES[i]);
                m_activeWcs = i;
                requestOffsets();
            }
        }

        if (isActive)
            ImGui::PopStyleColor(2);
    }

    if (!canSwitch)
        ImGui::EndDisabled();

    ImGui::TextDisabled("Active: %s", WCS_NAMES[m_activeWcs]);
}

void CncWcsPanel::renderOffsetDisplay() {
    ImGui::SeparatorText("Stored Offsets");

    if (!m_offsetsLoaded) {
        ImGui::TextDisabled("Offsets not loaded");
        if (ImGui::Button("Refresh Offsets") && m_cnc) {
            requestOffsets();
        }
        return;
    }

    if (ImGui::BeginTable("wcs_offsets", 4, ImGuiTableFlags_BordersInnerH |
                                              ImGuiTableFlags_RowBg)) {
        const auto& style = ImGui::GetStyle();
        float wcsColW = ImGui::CalcTextSize("G59").x + style.FramePadding.x * 2;
        ImGui::TableSetupColumn("WCS", ImGuiTableColumnFlags_WidthFixed, wcsColW);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < NUM_WCS; ++i) {
            ImGui::TableNextRow();

            // Highlight active row
            if (i == m_activeWcs) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                        ImGui::ColorConvertFloat4ToU32(
                                            ImVec4(0.2f, 0.4f, 0.6f, 0.3f)));
            }

            Vec3 offset = m_offsets.getByIndex(i);
            ImGui::TableNextColumn();
            ImGui::Text("%s", WCS_NAMES[i]);
            ImGui::TableNextColumn();
            ImGui::Text("%+.3f", static_cast<double>(offset.x));
            ImGui::TableNextColumn();
            ImGui::Text("%+.3f", static_cast<double>(offset.y));
            ImGui::TableNextColumn();
            ImGui::Text("%+.3f", static_cast<double>(offset.z));
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Refresh") && m_cnc) {
        requestOffsets();
    }
}

void CncWcsPanel::renderConfirmationPopup() {
    if (m_confirmZeroOpen) {
        ImGui::OpenPopup("Confirm Zero");
        m_confirmZeroOpen = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Confirm Zero", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_confirmZeroLabel.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Confirm button with warning color
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        float confirmBtnW = ImGui::CalcTextSize("Confirm").x + ImGui::GetStyle().FramePadding.x * 4;
        if (ImGui::Button("Confirm", ImVec2(confirmBtnW, 0))) {
            if (m_cnc) {
                m_cnc->sendCommand(m_confirmZeroCmd);
                requestOffsets(); // Refresh offsets after zeroing
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(confirmBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void CncWcsPanel::requestOffsets() {
    if (m_cnc) {
        m_cnc->sendCommand("$#");
        m_parsingOffsets = true;
        m_parsedWcsCount = 0;
        m_pendingOffsets = WcsOffsets{};
    }
}

void CncWcsPanel::onRawLine(const std::string& line, bool isSent) {
    if (isSent || !m_parsingOffsets)
        return;

    // Parse $# response lines: [G54:x.xxx,y.yyy,z.zzz]
    if (line.size() < 8 || line[0] != '[')
        return;

    // Find the closing bracket and colon
    auto closeBracket = line.find(']');
    auto colon = line.find(':');
    if (closeBracket == std::string::npos || colon == std::string::npos || colon < 1)
        return;

    std::string name = line.substr(1, colon - 1);
    std::string values = line.substr(colon + 1, closeBracket - colon - 1);

    float x = 0, y = 0, z = 0;
    if (std::sscanf(values.c_str(), "%f,%f,%f", &x, &y, &z) < 2)
        return;

    Vec3 offset{x, y, z};

    if (name == "G54") { m_pendingOffsets.g54 = offset; m_parsedWcsCount++; }
    else if (name == "G55") { m_pendingOffsets.g55 = offset; m_parsedWcsCount++; }
    else if (name == "G56") { m_pendingOffsets.g56 = offset; m_parsedWcsCount++; }
    else if (name == "G57") { m_pendingOffsets.g57 = offset; m_parsedWcsCount++; }
    else if (name == "G58") { m_pendingOffsets.g58 = offset; m_parsedWcsCount++; }
    else if (name == "G59") { m_pendingOffsets.g59 = offset; m_parsedWcsCount++; }
    else if (name == "G28") { m_pendingOffsets.g28 = offset; }
    else if (name == "G30") { m_pendingOffsets.g30 = offset; }
    else if (name == "G92") { m_pendingOffsets.g92 = offset; }
    else if (name == "TLO") { m_pendingOffsets.tlo = x; }

    // After parsing all 6 WCS entries, commit
    if (m_parsedWcsCount >= NUM_WCS) {
        m_offsets = m_pendingOffsets;
        m_offsetsLoaded = true;
        m_parsingOffsets = false;
    }
}

void CncWcsPanel::onStatusUpdate(const MachineStatus& status) {
    m_status = status;
}

void CncWcsPanel::onConnectionChanged(bool connected, const std::string& /*version*/) {
    m_connected = connected;
    if (!connected) {
        m_status = MachineStatus{};
        m_offsetsLoaded = false;
    } else {
        // Auto-request offsets on connect
        requestOffsets();
    }
}

} // namespace dw
