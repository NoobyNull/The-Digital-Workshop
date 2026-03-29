#include "ui/panels/cnc_status_panel.h"

#include <cstdio>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {

const char* machineStateName(MachineState state) {
    switch (state) {
    case MachineState::Idle: return "Idle";
    case MachineState::Run: return "Run";
    case MachineState::Hold: return "Hold";
    case MachineState::Jog: return "Jog";
    case MachineState::Alarm: return "Alarm";
    case MachineState::Door: return "Door";
    case MachineState::Check: return "Check";
    case MachineState::Home: return "Homing";
    case MachineState::Sleep: return "Sleep";
    case MachineState::Unknown: return "Unknown";
    default: return "???";
    }
}

ImVec4 machineStateColor(MachineState state) {
    switch (state) {
    case MachineState::Idle: return colors::kSuccess;                   // Green
    case MachineState::Run: return colors::kStateRun;                   // Blue
    case MachineState::Hold: return colors::kWarning;                   // Yellow
    case MachineState::Jog: return colors::kStateJog;                   // Light blue
    case MachineState::Alarm: return colors::kError;                    // Red
    case MachineState::Door: return colors::kStateDoor;                 // Orange
    case MachineState::Check: return colors::kStateCheck;               // Lavender
    case MachineState::Home: return colors::kStateHome;                 // Cyan
    case MachineState::Sleep: return colors::kDimmed;                   // Gray
    default: return colors::kDimmed;
    }
}

// Darker version of state color for background fill
ImU32 machineStateBgColor(MachineState state) {
    ImVec4 c = machineStateColor(state);
    // Darken to ~40% intensity for background
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * 0.4f, c.y * 0.4f, c.z * 0.4f, 0.9f));
}

} // anonymous namespace

CncStatusPanel::CncStatusPanel() : Panel("CNC Status") {}

void CncStatusPanel::render() {
    if (!m_open)
        return;

    applyMinSize(16, 8);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    if (!m_connected) {
        ImGui::Spacing();
        ImGui::TextColored(colors::kDimmed, "%s Disconnected", Icons::Unlink);
        if (!m_version.empty())
            ImGui::TextDisabled("Last firmware: %s", m_version.c_str());
        else
            ImGui::TextDisabled("Connect a CNC machine to see status");
        ImGui::End();
        return;
    }

    renderStateIndicator();
    renderProbeIndicator();
    renderAlarmBanner();
    ImGui::Spacing();
    renderDRO();
    ImGui::Spacing();
    renderFeedSpindle();
    renderOverrideControls();
    renderCoolantControls();
    renderMoveToDialog();

    ImGui::End();
}

void CncStatusPanel::renderStateIndicator() {
    ImVec4 stateColor = machineStateColor(m_status.state);
    ImU32 bgColor = machineStateBgColor(m_status.state);
    const char* stateName = machineStateName(m_status.state);

    // Draw a full-width colored banner for the state
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight() * 1.3f;

    ImGui::GetWindowDrawList()->AddRectFilled(
        cursorPos, ImVec2(cursorPos.x + width, cursorPos.y + height), bgColor, 4.0f);

    // Center the state text on the banner
    char label[64];
    std::snprintf(label, sizeof(label), "%s  %s", Icons::Info, stateName);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float textX = cursorPos.x + (width - textSize.x) * 0.5f;
    float textY = cursorPos.y + (height - textSize.y) * 0.5f;

    ImGui::GetWindowDrawList()->AddText(
        ImVec2(textX, textY), ImGui::ColorConvertFloat4ToU32(stateColor), label);

    // Advance cursor past the banner
    ImGui::Dummy(ImVec2(width, height));

    // Show firmware version in small text with WCS selector right-aligned
    if (!m_version.empty()) {
        ImGui::TextDisabled("GRBL %s", m_version.c_str());
    }
    renderWcsSelector();
}

void CncStatusPanel::renderProbeIndicator() {
    bool probeActive = (m_status.inputPins & cnc::PIN_PROBE) != 0;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = ImGui::GetFontSize() * 0.35f;
    ImU32 color = probeActive ? IM_COL32(0, 220, 0, 255) : IM_COL32(80, 80, 80, 255);
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(pos.x + radius + 2, pos.y + ImGui::GetTextLineHeight() * 0.5f),
        radius, color);
    ImGui::Dummy(ImVec2(radius * 2 + 4, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextDisabled("Probe");
    if (probeActive) {
        ImGui::SameLine();
        ImGui::TextColored(colors::kSuccess, "(ACTIVE)");
    }
}

void CncStatusPanel::renderDRO() {
    ImGui::SeparatorText("Position");

    auto& cfg = Config::instance();
    bool metric = cfg.getDisplayUnitsMetric();
    float uf = metric ? 1.0f : (1.0f / 25.4f);
    const char* pu = metric ? "mm" : "in";

    // Axis labels and colors
    static const char* axisNames[] = {"X", "Y", "Z"};
    static const char axisLetters[] = {'X', 'Y', 'Z'};
    static const ImVec4 axisColors[] = {
        colors::kAxisX, // X = Red
        colors::kAxisY, // Y = Green
        colors::kAxisZ, // Z = Blue
    };
    float workPos[] = {m_status.workPos.x * uf, m_status.workPos.y * uf, m_status.workPos.z * uf};
    float machPos[] = {m_status.machinePos.x * uf, m_status.machinePos.y * uf, m_status.machinePos.z * uf};

    // Work position — large digits, double-click to zero axis
    const auto& droStyle = ImGui::GetStyle();
    ImVec2 droSpacing(droStyle.ItemSpacing.x * 0.5f,
                      droStyle.ItemSpacing.y * 0.33f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, droSpacing);

    for (int i = 0; i < 3; ++i) {
        ImGui::SetWindowFontScale(2.0f);
        ImGui::TextColored(axisColors[i], "%s", axisNames[i]);
        ImGui::SameLine();

        // Render as Selectable so double-click is detectable
        char posLabel[32];
        if (metric)
            std::snprintf(posLabel, sizeof(posLabel), "%+10.3f##DRO%d",
                          static_cast<double>(workPos[i]), i);
        else
            std::snprintf(posLabel, sizeof(posLabel), "%+10.4f##DRO%d",
                          static_cast<double>(workPos[i]), i);
        if (ImGui::Selectable(posLabel, false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                // Zero this axis: G10 L20 P0 <axis>0
                if (m_cnc && m_connected && m_status.state == MachineState::Idle) {
                    char cmd[64];
                    std::snprintf(cmd, sizeof(cmd), "G10 L20 P0 %c0", axisLetters[i]);
                    m_cnc->sendCommand(cmd);
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SetTooltip("Double-click to zero %s axis", axisNames[i]);
            ImGui::SetWindowFontScale(2.0f);
        }
        ImGui::SetWindowFontScale(1.0f);
    }

    // Show unit label after DRO
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pu);

    ImGui::PopStyleVar();

    // Machine position — smaller, secondary
    ImGui::Spacing();
    ImGui::TextDisabled("Machine:");
    ImGui::SameLine();
    for (int i = 0; i < 3; ++i) {
        if (metric)
            ImGui::TextDisabled("%s %+.3f", axisNames[i], static_cast<double>(machPos[i]));
        else
            ImGui::TextDisabled("%s %+.4f", axisNames[i], static_cast<double>(machPos[i]));
        if (i < 2)
            ImGui::SameLine();
    }

    // Move To button
    bool canMove = m_cnc && m_connected && m_status.state == MachineState::Idle;
    if (!canMove) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Move To...")) {
        m_moveToX = m_status.workPos.x;
        m_moveToY = m_status.workPos.y;
        m_moveToZ = m_status.workPos.z;
        m_moveToOpen = true;
    }
    if (!canMove) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Move machine to specific coordinates");
    }
}

void CncStatusPanel::renderFeedSpindle() {
    ImGui::SeparatorText("Feed & Spindle");

    auto& cfg = Config::instance();
    bool metric = cfg.getDisplayUnitsMetric();
    float uf = metric ? 1.0f : (1.0f / 25.4f);
    const char* fu = metric ? "mm/min" : "in/min";

    if (ImGui::BeginTable("##feedspindle", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Feed", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Spindle", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableNextRow();

        // Feed rate — left column
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Feed Rate");
        ImGui::SetWindowFontScale(1.5f);
        ImGui::Text("%.0f", static_cast<double>(m_status.feedRate * uf));
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", fu);

        // Spindle speed — right column
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Spindle");
        ImGui::SetWindowFontScale(1.5f);
        ImGui::Text("%.0f", static_cast<double>(m_status.spindleSpeed));
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("RPM");

        ImGui::EndTable();
    }
}

void CncStatusPanel::renderOverrideControls() {
    ImGui::SeparatorText("Overrides");

    if (ImGui::BeginTable("##overrides", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.15f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.85f);

        // Feed override — slider (10-200%)
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Feed");
        ImGui::TableNextColumn();
        int feedOvr = m_status.feedOverride;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50);
        if (ImGui::SliderInt("##FeedOvr", &feedOvr, 10, 200, "%d%%")) {
            if (m_cnc) m_cnc->setFeedOverride(feedOvr);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("R##Feed")) {
            if (m_cnc) m_cnc->setFeedOverride(100);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to 100%%");

        // Spindle override — slider (10-200%)
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Spindle");
        ImGui::TableNextColumn();
        int spindleOvr = m_status.spindleOverride;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50);
        if (ImGui::SliderInt("##SpindleOvr", &spindleOvr, 10, 200, "%d%%")) {
            if (m_cnc) m_cnc->setSpindleOverride(spindleOvr);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("R##Spindle")) {
            if (m_cnc) m_cnc->setSpindleOverride(100);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to 100%%");

        // Rapid override — 3 buttons
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Rapid");
        ImGui::TableNextColumn();
        int rapidOvr = m_status.rapidOverride;
        auto rapidBtn = [&](const char* label, int pct) {
            bool active = (rapidOvr == pct);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(label)) {
                if (m_cnc) m_cnc->setRapidOverride(pct);
            }
            if (active) ImGui::PopStyleColor();
        };
        rapidBtn("25%##Rapid", 25);
        ImGui::SameLine();
        rapidBtn("50%##Rapid", 50);
        ImGui::SameLine();
        rapidBtn("100%##Rapid", 100);

        ImGui::EndTable();
    }
}

void CncStatusPanel::renderCoolantControls() {
    ImGui::SeparatorText("Coolant");

    bool canSend = m_cnc && m_connected &&
                   m_status.state != MachineState::Alarm;

    if (!canSend) ImGui::BeginDisabled();

    float btnWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;

    if (ImGui::Button("Flood (M8)", ImVec2(btnWidth, 0))) {
        if (m_cnc) m_cnc->sendCommand("M8");
    }
    ImGui::SameLine();
    if (ImGui::Button("Mist (M7)", ImVec2(btnWidth, 0))) {
        if (m_cnc) m_cnc->sendCommand("M7");
    }
    ImGui::SameLine();
    if (ImGui::Button("Off (M9)", ImVec2(btnWidth, 0))) {
        if (m_cnc) m_cnc->sendCommand("M9");
    }

    if (!canSend) ImGui::EndDisabled();
}

void CncStatusPanel::renderAlarmBanner() {
    if (m_status.state != MachineState::Alarm)
        return;

    ImGui::Spacing();

    // Red banner with alarm code and description
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight() * 2.2f;

    // Background derived from kError: darken the red, semi-transparent
    const ImVec4& ec = colors::kError;
    ImGui::GetWindowDrawList()->AddRectFilled(
        cursorPos, ImVec2(cursorPos.x + width, cursorPos.y + height),
        IM_COL32(static_cast<int>(ec.x * 180),
                 static_cast<int>(ec.y * 130),
                 static_cast<int>(ec.z * 130), 200),
        4.0f);

    // Alarm text
    char alarmText[128];
    if (m_lastAlarmCode > 0) {
        std::snprintf(alarmText, sizeof(alarmText), "ALARM %d: %s",
                      m_lastAlarmCode, m_lastAlarmDesc.c_str());
    } else {
        std::snprintf(alarmText, sizeof(alarmText), "ALARM (unknown code)");
    }
    ImVec2 textSize = ImGui::CalcTextSize(alarmText);
    float textX = cursorPos.x + ImGui::GetStyle().FramePadding.x;
    float textY = cursorPos.y + (height - textSize.y) * 0.5f - 8.0f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), alarmText);

    ImGui::Dummy(ImVec2(width, height - ImGui::GetFrameHeightWithSpacing()));

    // Inline unlock button
    float unlockBtnW = ImGui::CalcTextSize("Unlock ($X)").x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button("Unlock ($X)", ImVec2(unlockBtnW, 0))) {
        if (m_cnc) m_cnc->unlock();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Clear alarm state to continue");

    // Alarm reference tooltip
    ImGui::SameLine();
    ImGui::TextDisabled("?");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("GRBL Alarm Codes");
        ImGui::Separator();
        int count = 0;
        const auto* entries = alarmReference(count);
        for (int i = 0; i < count; ++i) {
            bool isActive = (entries[i].code == m_lastAlarmCode);
            if (isActive)
                ImGui::PushStyleColor(ImGuiCol_Text, colors::kError);
            ImGui::Text("ALARM %d: %s -- %s", entries[i].code,
                         entries[i].name, entries[i].description);
            if (isActive)
                ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
}

void CncStatusPanel::renderWcsSelector() {
    // WCS combo — compact, right-aligned next to firmware version
    bool canSwitch = m_cnc && m_connected &&
                     (m_status.state == MachineState::Idle ||
                      m_status.state == MachineState::Jog);

    if (!canSwitch) ImGui::BeginDisabled();

    auto& config = Config::instance();

    // Build display label for active WCS
    const auto& activeAlias = config.getWcsAlias(m_activeWcs);
    char activeLabel[80];
    if (!activeAlias.empty())
        std::snprintf(activeLabel, sizeof(activeLabel), "%s: %s",
                      WCS_NAMES[m_activeWcs], activeAlias.c_str());
    else
        std::snprintf(activeLabel, sizeof(activeLabel), "%s", WCS_NAMES[m_activeWcs]);

    // Size combo to fit the active label
    float wcsComboW = ImGui::CalcTextSize(activeLabel).x + ImGui::GetStyle().FramePadding.x * 4;
    float minW = ImGui::CalcTextSize("G59").x + ImGui::GetStyle().FramePadding.x * 4;
    if (wcsComboW < minW) wcsComboW = minW;

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - wcsComboW);
    ImGui::SetNextItemWidth(wcsComboW);
    if (ImGui::BeginCombo("##WCS", activeLabel, ImGuiComboFlags_NoArrowButton)) {
        for (int i = 0; i < NUM_WCS; ++i) {
            bool selected = (m_activeWcs == i);
            const auto& alias = config.getWcsAlias(i);

            // Inline rename on double-click
            if (m_editingWcsIdx == i) {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                char editId[32];
                std::snprintf(editId, sizeof(editId), "##wcsEdit%d", i);
                if (ImGui::InputText(editId, m_wcsAliasBuf, sizeof(m_wcsAliasBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    config.setWcsAlias(i, m_wcsAliasBuf);
                    config.save();
                    m_editingWcsIdx = -1;
                }
                // Cancel on Escape or losing focus
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                    (!ImGui::IsItemActive() && m_editingWcsIdx == i && ImGui::IsMouseClicked(0))) {
                    m_editingWcsIdx = -1;
                }
                // Auto-focus the input on first frame
                if (ImGui::IsItemDeactivated()) m_editingWcsIdx = -1;
                continue;
            }

            char itemLabel[80];
            if (!alias.empty())
                std::snprintf(itemLabel, sizeof(itemLabel), "%s: %s", WCS_NAMES[i], alias.c_str());
            else
                std::snprintf(itemLabel, sizeof(itemLabel), "%s", WCS_NAMES[i]);

            if (ImGui::Selectable(itemLabel, selected)) {
                m_activeWcs = i;
                if (m_cnc) {
                    m_cnc->sendCommand(WCS_NAMES[i]);
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();

            // Right-click context menu for rename / clear alias
            char ctxId[32];
            std::snprintf(ctxId, sizeof(ctxId), "##wcsCtx%d", i);
            if (ImGui::BeginPopupContextItem(ctxId)) {
                if (ImGui::MenuItem("Rename")) {
                    m_editingWcsIdx = i;
                    std::snprintf(m_wcsAliasBuf, sizeof(m_wcsAliasBuf), "%s", alias.c_str());
                }
                if (!alias.empty() && ImGui::MenuItem("Clear alias")) {
                    config.setWcsAlias(i, "");
                    config.save();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Work Coordinate System");
    }

    if (!canSwitch) ImGui::EndDisabled();
}

void CncStatusPanel::renderMoveToDialog() {
    if (m_moveToOpen) {
        ImGui::OpenPopup("Move To Position");
        m_moveToOpen = false;
    }
    if (ImGui::BeginPopupModal("Move To Position", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter target work coordinates:");
        ImGui::Spacing();

        float moveInputW = ImGui::GetFontSize() * 8;
        ImGui::SetNextItemWidth(moveInputW);
        ImGui::InputFloat("X##moveto", &m_moveToX, 0.1f, 1.0f, "%.3f");
        ImGui::SetNextItemWidth(moveInputW);
        ImGui::InputFloat("Y##moveto", &m_moveToY, 0.1f, 1.0f, "%.3f");
        ImGui::SetNextItemWidth(moveInputW);
        ImGui::InputFloat("Z##moveto", &m_moveToZ, 0.1f, 1.0f, "%.3f");

        ImGui::Spacing();
        ImGui::Checkbox("Rapid (G0)", &m_moveToUseG0);
        if (!m_moveToUseG0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(uses current feed rate)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool canGo = m_cnc && m_connected && m_status.state == MachineState::Idle;
        if (!canGo) ImGui::BeginDisabled();
        float goBtnW = ImGui::CalcTextSize("Go").x + ImGui::GetStyle().FramePadding.x * 4;
        float cancelBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        float moveBtnW = std::max(goBtnW, cancelBtnW);
        if (ImGui::Button("Go", ImVec2(moveBtnW, 0))) {
            char cmd[128];
            const char* moveCmd = m_moveToUseG0 ? "G0" : "G1";
            std::snprintf(cmd, sizeof(cmd), "G90 %s X%.3f Y%.3f Z%.3f",
                          moveCmd,
                          static_cast<double>(m_moveToX),
                          static_cast<double>(m_moveToY),
                          static_cast<double>(m_moveToZ));
            m_cnc->sendCommand(cmd);
            ImGui::CloseCurrentPopup();
        }
        if (!canGo) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(moveBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void CncStatusPanel::onAlarm(int alarmCode, const std::string& desc) {
    m_lastAlarmCode = alarmCode;
    m_lastAlarmDesc = desc;
}

void CncStatusPanel::onStatusUpdate(const MachineStatus& status) {
    m_status = status;
}

void CncStatusPanel::onConnectionChanged(bool connected, const std::string& version) {
    m_connected = connected;
    m_version = version;
    if (!connected) {
        // Reset status to defaults on disconnect
        m_status = MachineStatus{};
    }
}

} // namespace dw
