#include "ui/panels/cnc_safety_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "core/cnc/cnc_types.h"
#include "core/cnc/preflight_check.h"
#include "core/config/config.h"
#include "core/gcode/gcode_modal_scanner.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {
struct LongPressButton {
    bool holding = false;
    float holdTime = 0.0f;

    bool render(const char* label, ImVec2 size, float requiredMs, bool enabled) {
        if (!enabled) ImGui::BeginDisabled();
        ImGui::Button(label, size);
        if (!enabled) ImGui::EndDisabled();
        if (!enabled) { holding = false; holdTime = 0.0f; return false; }

        bool isHeld = ImGui::IsItemActive();
        if (isHeld) {
            holdTime += ImGui::GetIO().DeltaTime * 1000.0f;
            float progress = holdTime / requiredMs;
            if (progress > 1.0f) progress = 1.0f;
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 fillMax = {rmin.x + (rmax.x - rmin.x) * progress, rmax.y};
            ImGui::GetWindowDrawList()->AddRectFilled(
                rmin, fillMax, IM_COL32(255, 255, 255, 40), 3.0f);
            holding = true;
        } else {
            if (holding) { holding = false; holdTime = 0.0f; }
        }
        if (holdTime >= requiredMs) {
            holding = false;
            holdTime = 0.0f;
            return true;
        }
        return false;
    }
};

static LongPressButton s_abortLongPress;
} // namespace

CncSafetyPanel::CncSafetyPanel() : Panel("Safety") {}

void CncSafetyPanel::render() {
    if (!m_open)
        return;

    applyMinSize(18, 10);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    // Pause-before-reset abort sequence: feed hold first, then soft reset after delay
    if (m_abortPending) {
        m_abortTimer += ImGui::GetIO().DeltaTime;
        if (m_abortTimer >= 0.2f) {
            if (m_cnc) m_cnc->softReset();
            m_abortPending = false;
            m_abortTimer = 0.0f;
        }
    }

    renderSafetyControls();
    ImGui::Separator();
    renderDrawOutline();
    ImGui::Separator();
    renderSensorDisplay();

    // Probe workflows button
    ImGui::Separator();
    ImGui::SeparatorText("Probing");
    if (ImGui::Button("Probe Workflows...", ImVec2(-1, 0))) {
        m_probeDialogOpen = true;
    }

    renderAbortConfirmDialog();
    renderResumeDialog();
    renderProbeDialog();

    ImGui::End();
}

void CncSafetyPanel::renderSafetyControls() {
    ImGui::SeparatorText("Job Control");
    const auto& style = ImGui::GetStyle();
    float safetyBtnH = ImGui::GetFrameHeight() * 1.5f;

    // --- Pause button ---
    bool canPause = m_connected &&
                    (m_status.state == MachineState::Run ||
                     m_status.state == MachineState::Jog);

    if (!canPause)
        ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, canPause
        ? ImVec4(0.85f, 0.65f, 0.13f, 1.0f)   // Amber when enabled
        : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));     // Grey when disabled
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.75f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.55f, 0.10f, 1.0f));

    std::string pauseLabel = std::string(Icons::Pause) + " Pause";
    float pauseW = ImGui::CalcTextSize(pauseLabel.c_str()).x
                   + style.FramePadding.x * 4;
    if (ImGui::Button(pauseLabel.c_str(), ImVec2(pauseW, safetyBtnH))) {
        if (m_cnc) m_cnc->feedHold();
    }

    ImGui::PopStyleColor(3);
    if (!canPause)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // --- Resume button ---
    bool canResume = m_connected &&
                     m_status.state == MachineState::Hold &&
                     !isDoorInterlockActive();

    if (!canResume)
        ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, canResume
        ? ImVec4(0.2f, 0.7f, 0.3f, 1.0f)      // Green when enabled
        : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));     // Grey when disabled
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.25f, 1.0f));

    std::string resumeLabel = std::string(Icons::Play) + " Resume";
    float resumeW = ImGui::CalcTextSize(resumeLabel.c_str()).x
                    + style.FramePadding.x * 4;
    if (ImGui::Button(resumeLabel.c_str(), ImVec2(resumeW, safetyBtnH))) {
        if (m_cnc) m_cnc->cycleStart();
    }

    ImGui::PopStyleColor(3);
    if (!canResume)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // --- Abort button (CRITICAL: labeled "Abort", NEVER "E-Stop") ---
    bool canAbort = m_connected;
    auto& cfg = Config::instance();
    bool abortUseLongPress = cfg.getSafetyAbortLongPress();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));

    if (abortUseLongPress && m_streaming) {
        // Long-press abort — the hold IS the confirmation
        float durationMs = static_cast<float>(cfg.getSafetyLongPressDurationMs());
        std::string abortLabel = std::string(Icons::Stop) + " Hold to Abort";
        float abortW = ImGui::CalcTextSize(abortLabel.c_str()).x
                       + style.FramePadding.x * 4;
        if (s_abortLongPress.render(abortLabel.c_str(),
                ImVec2(abortW, safetyBtnH), durationMs, canAbort)) {
            if (m_cnc) {
                if (cfg.getSafetyPauseBeforeResetEnabled()) {
                    m_cnc->feedHold();
                    m_abortPending = true;
                    m_abortTimer = 0.0f;
                } else {
                    m_cnc->softReset();
                }
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Hold for %.1fs to abort running job", static_cast<double>(durationMs / 1000.0f));
        }
    } else {
        if (!canAbort)
            ImGui::BeginDisabled();
        std::string abortLabel = std::string(Icons::Stop) + " Abort";
        float abortW = ImGui::CalcTextSize(abortLabel.c_str()).x
                       + style.FramePadding.x * 4;
        if (ImGui::Button(abortLabel.c_str(), ImVec2(abortW, safetyBtnH))) {
            if (m_streaming) {
                // Show confirmation dialog when a job is running
                m_showAbortConfirm = true;
            } else {
                // Direct soft reset when no job is running
                if (m_cnc) m_cnc->softReset();
            }
        }
        if (!canAbort)
            ImGui::EndDisabled();
    }

    ImGui::PopStyleColor(3);

    // --- Resume From Line button ---
    ImGui::Spacing();
    bool canResumeFromLine = m_connected && !m_streaming && hasProgram();
    if (!canResumeFromLine)
        ImGui::BeginDisabled();
    if (ImGui::Button("Resume From Line...", ImVec2(-1, 0))) {
        m_showResumeDialog = true;
        m_preambleGenerated = false;
        m_preambleLines.clear();
        m_preflightIssues.clear();
        m_resumeLine = 1;
    }
    if (!canResumeFromLine)
        ImGui::EndDisabled();

    // Safety note
    ImGui::Spacing();
    ImGui::TextDisabled(
        "Software stop only -- ensure hardware E-stop is accessible");
}

void CncSafetyPanel::renderAbortConfirmDialog() {
    if (m_showAbortConfirm) {
        ImGui::OpenPopup("Abort Running Job?");
        m_showAbortConfirm = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Abort Running Job?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(colors::kErrorText,
                           "%s WARNING", Icons::Warning);
        ImGui::Spacing();
        ImGui::TextWrapped(
            "A job is currently running. Aborting will send a soft reset "
            "(0x18) which stops all motion immediately.\n\n"
            "You may need to re-home the machine afterward.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Abort Job button (red)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        float abortDlgBtnW = ImGui::CalcTextSize("Abort Job").x + ImGui::GetStyle().FramePadding.x * 4;
        if (ImGui::Button("Abort Job", ImVec2(abortDlgBtnW, 0))) {
            if (m_cnc) {
                if (Config::instance().getSafetyPauseBeforeResetEnabled()) {
                    // Pause-before-reset: feed hold first, soft reset after 200ms
                    m_cnc->feedHold();
                    m_abortPending = true;
                    m_abortTimer = 0.0f;
                } else {
                    m_cnc->softReset();
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // Cancel button
        if (ImGui::Button("Cancel", ImVec2(abortDlgBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void CncSafetyPanel::renderDrawOutline() {
    ImGui::SeparatorText("Draw Outline");

    if (!m_hasBounds) {
        ImGui::TextDisabled("Load a G-code file to enable outline tracing");
        return;
    }

    auto& cfg = Config::instance();
    bool metric = cfg.getDisplayUnitsMetric();
    float uf = metric ? 1.0f : (1.0f / 25.4f);
    const char* pu = metric ? "mm" : "in";
    const char* fu = metric ? "mm/min" : "in/min";

    // Show bounding box dimensions
    float sizeX = m_boundsMax.x - m_boundsMin.x;
    float sizeY = m_boundsMax.y - m_boundsMin.y;
    ImGui::Text("Job bounds: %.1f x %.1f %s", static_cast<double>(sizeX * uf),
                static_cast<double>(sizeY * uf), pu);
    ImGui::TextDisabled("  X: %.1f to %.1f  Y: %.1f to %.1f",
                        static_cast<double>(m_boundsMin.x * uf),
                        static_cast<double>(m_boundsMax.x * uf),
                        static_cast<double>(m_boundsMin.y * uf),
                        static_cast<double>(m_boundsMax.y * uf));

    ImGui::Spacing();

    // Safe Z height input (stored in mm, displayed in user units)
    ImGui::Text("Safe Z height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::InputFloat("##safeZ", &m_outlineSafeZ, 0.0f, 0.0f, "%.1f");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pu);
    if (m_outlineSafeZ < 0.0f) m_outlineSafeZ = 0.0f;
    if (m_outlineSafeZ > 100.0f) m_outlineSafeZ = 100.0f;

    // Feed rate input (stored in mm/min, displayed in user units)
    ImGui::Text("Travel speed:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::InputFloat("##outlineFeed", &m_outlineFeedRate, 0.0f, 0.0f, "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", fu);
    if (m_outlineFeedRate < 100.0f) m_outlineFeedRate = 100.0f;
    if (m_outlineFeedRate > 10000.0f) m_outlineFeedRate = 10000.0f;

    ImGui::Spacing();

    // Draw outline button
    bool canDraw = m_connected && !m_streaming &&
                   m_status.state == MachineState::Idle;

    if (!canDraw)
        ImGui::BeginDisabled();

    if (ImGui::Button("Draw Outline", ImVec2(-1, 28))) {
        if (m_cnc) {
            // Generate outline G-code: raise to safe Z, trace bounding box, return
            char cmd[128];

            // Switch to absolute mode and raise Z to safe height
            m_cnc->sendCommand("G90");
            std::snprintf(cmd, sizeof(cmd), "G0 Z%.1f",
                          static_cast<double>(m_outlineSafeZ));
            m_cnc->sendCommand(cmd);

            // Move to first corner (min X, min Y) at rapid
            std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                          static_cast<double>(m_boundsMin.x),
                          static_cast<double>(m_boundsMin.y));
            m_cnc->sendCommand(cmd);

            // Trace rectangle at feed rate
            std::snprintf(cmd, sizeof(cmd), "G1 X%.3f Y%.3f F%.0f",
                          static_cast<double>(m_boundsMax.x),
                          static_cast<double>(m_boundsMin.y),
                          static_cast<double>(m_outlineFeedRate));
            m_cnc->sendCommand(cmd);

            std::snprintf(cmd, sizeof(cmd), "G1 X%.3f Y%.3f",
                          static_cast<double>(m_boundsMax.x),
                          static_cast<double>(m_boundsMax.y));
            m_cnc->sendCommand(cmd);

            std::snprintf(cmd, sizeof(cmd), "G1 X%.3f Y%.3f",
                          static_cast<double>(m_boundsMin.x),
                          static_cast<double>(m_boundsMax.y));
            m_cnc->sendCommand(cmd);

            std::snprintf(cmd, sizeof(cmd), "G1 X%.3f Y%.3f",
                          static_cast<double>(m_boundsMin.x),
                          static_cast<double>(m_boundsMin.y));
            m_cnc->sendCommand(cmd);

            // Return to work zero XY at rapid, keep Z at safe height
            m_cnc->sendCommand("G0 X0 Y0");
        }
    }

    if (!canDraw)
        ImGui::EndDisabled();

    if (!m_connected) {
        ImGui::TextDisabled("Connect to CNC to use");
    } else if (m_streaming) {
        ImGui::TextDisabled("Cannot draw during active job");
    } else if (m_status.state != MachineState::Idle) {
        ImGui::TextDisabled("Machine must be idle");
    }
}

void CncSafetyPanel::renderSensorDisplay() {
    ImGui::SeparatorText("Input Pins");

    if (!m_connected) {
        ImGui::TextDisabled("Not connected");
        return;
    }

    // Door interlock warning banner
    if (isDoorInterlockActive()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors::kWarning);
        ImGui::TextWrapped("%s DOOR INTERLOCK ACTIVE -- Rapid moves and spindle "
                           "commands are blocked until door is closed", Icons::Warning);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Helper lambda for pin indicator
    auto pinIndicator = [&](const char* label, u32 pinMask, ImVec4 activeColor) {
        bool active = (m_status.inputPins & pinMask) != 0;
        ImVec4 color = active ? activeColor : ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s %s", active ? Icons::Warning : "  ", label);
    };

    // Limit switches (red when active -- indicates potential issue)
    ImVec4 limitColor = colors::kError;
    if (ImGui::BeginTable("##limitpins", 3, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        pinIndicator("X Limit", cnc::PIN_X_LIMIT, limitColor);
        ImGui::TableNextColumn();
        pinIndicator("Y Limit", cnc::PIN_Y_LIMIT, limitColor);
        ImGui::TableNextColumn();
        pinIndicator("Z Limit", cnc::PIN_Z_LIMIT, limitColor);
        ImGui::EndTable();
    }

    // Probe (green when active) and Door (yellow when active)
    ImVec4 probeColor = colors::kSuccess;
    ImVec4 doorColor = colors::kWarning;
    if (ImGui::BeginTable("##otherpins", 3, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col3", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        pinIndicator("Probe", cnc::PIN_PROBE, probeColor);
        ImGui::TableNextColumn();
        pinIndicator("Door", cnc::PIN_DOOR, doorColor);
        ImGui::TableNextColumn();
        // Empty third column for alignment
        ImGui::EndTable();
    }
}

void CncSafetyPanel::renderResumeDialog() {
    if (m_showResumeDialog) {
        ImGui::OpenPopup("Resume From Line");
        m_showResumeDialog = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.35f, 0), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Resume From Line", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize))
        return;

    int totalLines = static_cast<int>(m_fullProgram.size());

    // --- Line number input ---
    ImGui::Text("Line number:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    if (ImGui::InputInt("##resumeline", &m_resumeLine)) {
        // Clamp to valid range
        if (m_resumeLine < 1) m_resumeLine = 1;
        if (m_resumeLine > totalLines) m_resumeLine = totalLines;
        m_preambleGenerated = false; // Reset on line change
    }
    ImGui::SameLine();
    ImGui::TextDisabled("of %d lines", totalLines);

    ImGui::Spacing();

    // --- Generate Preamble button ---
    if (ImGui::Button("Generate Preamble", ImVec2(-1, 0))) {
        // Convert 1-based display to 0-based for scanner
        auto state = GCodeModalScanner::scanToLine(
            m_fullProgram, m_resumeLine - 1);
        m_preambleLines = state.toPreamble();
        m_preambleGenerated = true;

        // Run pre-flight checks
        if (m_cnc) {
            m_preflightIssues = runPreflightChecks(
                *m_cnc, false, false);
        }
    }

    // --- Preamble preview ---
    if (m_preambleGenerated) {
        ImGui::Spacing();
        ImGui::SeparatorText("Modal State Preamble");
        ImGui::TextDisabled(
            "%d lines will be sent before resuming:",
            static_cast<int>(m_preambleLines.size()));

        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        float preambleH = ImGui::GetTextLineHeightWithSpacing() * 6;
        if (ImGui::BeginChild("##preamble", ImVec2(-1, preambleH), true)) {
            for (const auto& line : m_preambleLines) {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::TextDisabled(
            "These commands restore machine state to line %d",
            m_resumeLine);
    }

    // --- Pre-flight check results ---
    bool hasErrors = false;
    if (m_preambleGenerated && !m_preflightIssues.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Pre-flight Checks");
        for (const auto& issue : m_preflightIssues) {
            if (issue.severity == PreflightIssue::Error) {
                hasErrors = true;
                ImGui::TextColored(
                    colors::kError,
                    "%s %s", Icons::Error, issue.message.c_str());
            } else {
                ImGui::TextColored(
                    colors::kWarning,
                    "%s %s", Icons::Warning, issue.message.c_str());
            }
        }
    }

    // --- Warning text ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(colors::kOrange,
                       "%s CAUTION", Icons::Warning);
    ImGui::TextWrapped(
        "Resuming from an arbitrary line is inherently risky. "
        "Verify the preamble restores correct machine state "
        "before proceeding.");
    ImGui::TextWrapped(
        "Arc commands (G2/G3) at the resume point may not "
        "execute correctly.");

    // --- Action buttons ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Resume button (green, only when preamble generated and no errors)
    bool canResume = m_preambleGenerated && !hasErrors;
    if (!canResume)
        ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.15f, 0.6f, 0.25f, 1.0f));

    float resumeDlgBtnW = ImGui::CalcTextSize("Resume").x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button("Resume", ImVec2(resumeDlgBtnW, 0))) {
        // Build combined program: preamble + remaining lines
        std::vector<std::string> combined;
        combined.reserve(
            m_preambleLines.size() +
            m_fullProgram.size() - static_cast<size_t>(m_resumeLine - 1));

        for (const auto& line : m_preambleLines)
            combined.push_back(line);

        for (int i = m_resumeLine - 1;
             i < static_cast<int>(m_fullProgram.size()); ++i) {
            combined.push_back(m_fullProgram[static_cast<size_t>(i)]);
        }

        if (m_cnc)
            (void)m_cnc->startStream(combined);

        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    if (!canResume)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // Cancel button
    if (ImGui::Button("Cancel", ImVec2(resumeDlgBtnW, 0))) {
        m_preambleGenerated = false;
        m_preambleLines.clear();
        m_preflightIssues.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void CncSafetyPanel::renderProbeDialog() {
    if (m_probeDialogOpen) {
        ImGui::OpenPopup("Probe Workflows");
        m_probeDialogOpen = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Probe Workflows", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (ImGui::BeginTabBar("ProbeTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Z-Probe")) {
            renderZProbeTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tool Length")) {
            renderTlsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("3D Probing")) {
            render3DProbeTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    float closeBtnW = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button("Close", ImVec2(closeBtnW, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void CncSafetyPanel::renderZProbeTab() {
    ImGui::TextWrapped("Touch off Z-zero using a probe or touch plate.");
    ImGui::Spacing();

    auto& cfg = Config::instance();
    bool metric = cfg.getDisplayUnitsMetric();
    const char* pu = metric ? "mm" : "in";
    const char* fu = metric ? "mm/min" : "in/min";

    char speedLabel[64], thickLabel[64], searchLabel[64], retractLabel[64];
    std::snprintf(speedLabel, sizeof(speedLabel), "Approach Speed (%s)", fu);
    std::snprintf(thickLabel, sizeof(thickLabel), "Plate Thickness (%s)", pu);
    std::snprintf(searchLabel, sizeof(searchLabel), "Search Distance (%s)", pu);
    std::snprintf(retractLabel, sizeof(retractLabel), "Retract Distance (%s)", pu);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(speedLabel, &m_probeApproachSpeed, 10, 50, "%.0f");
    m_probeApproachSpeed = std::clamp(m_probeApproachSpeed, 1.0f, 1000.0f);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(thickLabel, &m_probePlateThickness, 0.1f, 1.0f, "%.3f");
    m_probePlateThickness = std::max(0.0f, m_probePlateThickness);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(searchLabel, &m_probeSearchDist, 5, 10, "%.1f");
    m_probeSearchDist = std::clamp(m_probeSearchDist, 1.0f, 200.0f);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(retractLabel, &m_probeRetractDist, 0.5f, 1, "%.1f");
    m_probeRetractDist = std::clamp(m_probeRetractDist, 0.1f, 20.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Preview the commands that will be sent
    ImGui::TextDisabled("Commands:");
    ImGui::Text("G21 G91");
    ImGui::Text("G38.2 Z-%.1f F%.0f",
                static_cast<double>(m_probeSearchDist),
                static_cast<double>(m_probeApproachSpeed));
    if (m_probeRetractDist > 0) {
        ImGui::Text("G0 Z%.1f", static_cast<double>(m_probeRetractDist));
        ImGui::Text("G38.2 Z-%.1f F%.0f",
                    static_cast<double>(m_probeRetractDist + 1.0f),
                    static_cast<double>(m_probeApproachSpeed * 0.5f));
    }
    ImGui::Text("G10 L20 P0 Z%.3f", static_cast<double>(m_probePlateThickness));
    ImGui::Text("G0 Z%.1f", static_cast<double>(m_probeRetractDist));
    ImGui::Text("G90");

    ImGui::Spacing();

    bool canProbe = m_cnc && m_connected &&
                    m_status.state == MachineState::Idle;
    if (!canProbe) ImGui::BeginDisabled();
    float zProbeW = ImGui::CalcTextSize("Run Z-Probe").x
                    + ImGui::GetStyle().FramePadding.x * 4;
    float probeBtnH = ImGui::GetFrameHeight() * 1.5f;
    if (ImGui::Button("Run Z-Probe", ImVec2(zProbeW, probeBtnH))) {
        char cmd[256];
        m_cnc->sendCommand("G21 G91");
        std::snprintf(cmd, sizeof(cmd), "G38.2 Z-%.1f F%.0f",
                      static_cast<double>(m_probeSearchDist),
                      static_cast<double>(m_probeApproachSpeed));
        m_cnc->sendCommand(cmd);

        if (m_probeRetractDist > 0) {
            std::snprintf(cmd, sizeof(cmd), "G0 Z%.1f",
                          static_cast<double>(m_probeRetractDist));
            m_cnc->sendCommand(cmd);
            // Second slower probe for accuracy
            std::snprintf(cmd, sizeof(cmd), "G38.2 Z-%.1f F%.0f",
                          static_cast<double>(m_probeRetractDist + 1.0f),
                          static_cast<double>(m_probeApproachSpeed * 0.5f));
            m_cnc->sendCommand(cmd);
        }

        // Set Z zero accounting for plate thickness
        std::snprintf(cmd, sizeof(cmd), "G10 L20 P0 Z%.3f",
                      static_cast<double>(m_probePlateThickness));
        m_cnc->sendCommand(cmd);

        // Retract
        std::snprintf(cmd, sizeof(cmd), "G0 Z%.1f",
                      static_cast<double>(m_probeRetractDist));
        m_cnc->sendCommand(cmd);

        m_cnc->sendCommand("G90");
    }
    if (!canProbe) ImGui::EndDisabled();

    if (!canProbe) {
        ImGui::TextColored(colors::kOrange,
                           "Machine must be Idle and connected to probe");
    }
}

void CncSafetyPanel::renderTlsTab() {
    ImGui::TextWrapped(
        "Measure tool length offset. Touch the tool to a fixed reference surface, "
        "then apply G43.1 compensation.");
    ImGui::Spacing();

    {
    auto& cfg = Config::instance();
    const char* pu = cfg.getDisplayUnitsMetric() ? "mm" : "in";
    const char* fu = cfg.getDisplayUnitsMetric() ? "mm/min" : "in/min";

    char tlsSpeedLabel[64], tlsSearchLabel[64];
    std::snprintf(tlsSpeedLabel, sizeof(tlsSpeedLabel), "Approach Speed (%s)##tls", fu);
    std::snprintf(tlsSearchLabel, sizeof(tlsSearchLabel), "Search Distance (%s)##tls", pu);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(tlsSpeedLabel, &m_tlsApproachSpeed, 10, 50, "%.0f");
    m_tlsApproachSpeed = std::clamp(m_tlsApproachSpeed, 1.0f, 500.0f);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(tlsSearchLabel, &m_tlsSearchDist, 10, 50, "%.0f");
    m_tlsSearchDist = std::clamp(m_tlsSearchDist, 1.0f, 300.0f);

    ImGui::Spacing();
    ImGui::SeparatorText("Workflow");

    ImGui::TextWrapped(
        "1. Set reference: Probe first tool (reference tool) to touch plate. "
        "Record Z position as reference.");

    char refZLabel[64];
    std::snprintf(refZLabel, sizeof(refZLabel), "Reference Z (%s)", pu);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(refZLabel, &m_tlsReferenceZ, 0.1f, 1.0f, "%.3f");

    ImGui::SameLine();
    bool canCapture = m_cnc && m_connected && m_status.state == MachineState::Idle;
    if (!canCapture) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Capture Current Z")) {
        m_tlsReferenceZ = m_status.machinePos.z;
    }
    if (!canCapture) ImGui::EndDisabled();

    ImGui::TextWrapped("2. Probe new tool: Run probe cycle with new tool installed.");
    ImGui::TextWrapped("3. The offset (difference from reference) is applied via G43.1.");

    ImGui::Spacing();

    // Preview
    ImGui::TextDisabled("Commands:");
    ImGui::Text("G21 G91");
    ImGui::Text("G38.2 Z-%.0f F%.0f",
                static_cast<double>(m_tlsSearchDist),
                static_cast<double>(m_tlsApproachSpeed));
    ImGui::Text("G43.1 Z[measured - %.3f]", static_cast<double>(m_tlsReferenceZ));
    ImGui::Text("G90");

    ImGui::Spacing();

    if (!canCapture) ImGui::BeginDisabled();
    float tlsBtnW = ImGui::CalcTextSize("Probe & Set Tool Length").x
                    + ImGui::GetStyle().FramePadding.x * 4;
    float tlsBtnH = ImGui::GetFrameHeight() * 1.5f;
    if (ImGui::Button("Probe & Set Tool Length", ImVec2(tlsBtnW, tlsBtnH))) {
        char cmd[256];
        m_cnc->sendCommand("G21 G91");
        std::snprintf(cmd, sizeof(cmd), "G38.2 Z-%.0f F%.0f",
                      static_cast<double>(m_tlsSearchDist),
                      static_cast<double>(m_tlsApproachSpeed));
        m_cnc->sendCommand(cmd);
        m_cnc->sendCommand("G90");

        // Apply G43.1 offset: measured Z - reference Z
        // Note: This simplified approach uses the current machine Z position.
        // A full implementation would read the probe result position from
        // the G38.2 response before calculating the offset.
        std::snprintf(cmd, sizeof(cmd), "G43.1 Z%.3f",
                      static_cast<double>(m_status.machinePos.z - m_tlsReferenceZ));
        m_cnc->sendCommand(cmd);
    }
    if (!canCapture) ImGui::EndDisabled();

    if (!canCapture) {
        ImGui::TextColored(colors::kOrange,
                           "Machine must be Idle and connected");
    }
    } // end unit label scope
}

void CncSafetyPanel::render3DProbeTab() {
    ImGui::TextWrapped(
        "Find workpiece edges, corners, or center using probe sequences.");
    ImGui::Spacing();

    auto& cfg = Config::instance();
    const char* pu = cfg.getDisplayUnitsMetric() ? "mm" : "in";
    const char* fu = cfg.getDisplayUnitsMetric() ? "mm/min" : "in/min";

    char probeSpeedLbl[64], retractLbl[64], searchLbl[64];
    std::snprintf(probeSpeedLbl, sizeof(probeSpeedLbl), "Probe Speed (%s)##3d", fu);
    std::snprintf(retractLbl, sizeof(retractLbl), "Retract Dist (%s)##3d", pu);
    std::snprintf(searchLbl, sizeof(searchLbl), "Search Dist (%s)##3d", pu);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(probeSpeedLbl, &m_3dProbeSpeed, 10, 50, "%.0f");
    m_3dProbeSpeed = std::clamp(m_3dProbeSpeed, 1.0f, 1000.0f);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(retractLbl, &m_3dProbeRetract, 1, 5, "%.1f");
    m_3dProbeRetract = std::clamp(m_3dProbeRetract, 0.5f, 50.0f);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputFloat(searchLbl, &m_3dProbeSearchDist, 5, 10, "%.0f");
    m_3dProbeSearchDist = std::clamp(m_3dProbeSearchDist, 1.0f, 200.0f);

    ImGui::Spacing();
    ImGui::SeparatorText("Probe Operation");

    const char* modes[] = {"Edge X", "Edge Y", "Corner (X+Y)", "Center (X)"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    ImGui::Combo("Mode##3d", &m_3dProbeMode, modes, 4);

    ImGui::Spacing();

    // Show description and expected commands for selected mode
    switch (m_3dProbeMode) {
    case 0: // Edge X
        ImGui::TextWrapped(
            "Find X edge: Probes in -X direction from current position. "
            "Sets X zero at contact point.");
        ImGui::TextDisabled("G38.2 X-%.0f F%.0f",
                            static_cast<double>(m_3dProbeSearchDist),
                            static_cast<double>(m_3dProbeSpeed));
        ImGui::TextDisabled("G10 L20 P0 X0");
        break;
    case 1: // Edge Y
        ImGui::TextWrapped(
            "Find Y edge: Probes in -Y direction from current position. "
            "Sets Y zero at contact point.");
        ImGui::TextDisabled("G38.2 Y-%.0f F%.0f",
                            static_cast<double>(m_3dProbeSearchDist),
                            static_cast<double>(m_3dProbeSpeed));
        ImGui::TextDisabled("G10 L20 P0 Y0");
        break;
    case 2: // Corner
        ImGui::TextWrapped(
            "Find corner: Probes X then Y edges sequentially. "
            "Sets both X and Y zero at the corner.");
        ImGui::TextDisabled("G38.2 X-%.0f F%.0f  (find X edge)",
                            static_cast<double>(m_3dProbeSearchDist),
                            static_cast<double>(m_3dProbeSpeed));
        ImGui::TextDisabled("G0 X%.0f  (retract X)",
                            static_cast<double>(m_3dProbeRetract));
        ImGui::TextDisabled("G38.2 Y-%.0f F%.0f  (find Y edge)",
                            static_cast<double>(m_3dProbeSearchDist),
                            static_cast<double>(m_3dProbeSpeed));
        ImGui::TextDisabled("G10 L20 P0 X0 Y0");
        break;
    case 3: // Center X
        ImGui::TextWrapped(
            "Find center X: Probes +X then -X from the current position. "
            "Sets X zero at the midpoint between contacts.");
        ImGui::TextDisabled("G38.2 X+%.0f F%.0f  (right edge)",
                            static_cast<double>(m_3dProbeSearchDist),
                            static_cast<double>(m_3dProbeSpeed));
        ImGui::TextDisabled("Retract, then G38.2 X-%.0f  (left edge)",
                            static_cast<double>(m_3dProbeSearchDist * 2));
        ImGui::TextDisabled("G10 L20 P0 X[midpoint]");
        break;
    }

    ImGui::Spacing();

    bool canProbe = m_cnc && m_connected && m_status.state == MachineState::Idle;
    if (!canProbe) ImGui::BeginDisabled();
    float runProbeW = ImGui::CalcTextSize("Run Probe").x
                      + ImGui::GetStyle().FramePadding.x * 4;
    float runProbeBtnH = ImGui::GetFrameHeight() * 1.5f;
    if (ImGui::Button("Run Probe", ImVec2(runProbeW, runProbeBtnH))) {
        char cmd[256];
        m_cnc->sendCommand("G21 G91");

        switch (m_3dProbeMode) {
        case 0: // Edge X
            std::snprintf(cmd, sizeof(cmd), "G38.2 X-%.0f F%.0f",
                          static_cast<double>(m_3dProbeSearchDist),
                          static_cast<double>(m_3dProbeSpeed));
            m_cnc->sendCommand(cmd);
            m_cnc->sendCommand("G10 L20 P0 X0");
            std::snprintf(cmd, sizeof(cmd), "G0 X%.1f",
                          static_cast<double>(m_3dProbeRetract));
            m_cnc->sendCommand(cmd);
            break;
        case 1: // Edge Y
            std::snprintf(cmd, sizeof(cmd), "G38.2 Y-%.0f F%.0f",
                          static_cast<double>(m_3dProbeSearchDist),
                          static_cast<double>(m_3dProbeSpeed));
            m_cnc->sendCommand(cmd);
            m_cnc->sendCommand("G10 L20 P0 Y0");
            std::snprintf(cmd, sizeof(cmd), "G0 Y%.1f",
                          static_cast<double>(m_3dProbeRetract));
            m_cnc->sendCommand(cmd);
            break;
        case 2: // Corner
            // Probe X edge
            std::snprintf(cmd, sizeof(cmd), "G38.2 X-%.0f F%.0f",
                          static_cast<double>(m_3dProbeSearchDist),
                          static_cast<double>(m_3dProbeSpeed));
            m_cnc->sendCommand(cmd);
            m_cnc->sendCommand("G10 L20 P0 X0");
            std::snprintf(cmd, sizeof(cmd), "G0 X%.1f",
                          static_cast<double>(m_3dProbeRetract));
            m_cnc->sendCommand(cmd);
            // Probe Y edge
            std::snprintf(cmd, sizeof(cmd), "G38.2 Y-%.0f F%.0f",
                          static_cast<double>(m_3dProbeSearchDist),
                          static_cast<double>(m_3dProbeSpeed));
            m_cnc->sendCommand(cmd);
            m_cnc->sendCommand("G10 L20 P0 Y0");
            std::snprintf(cmd, sizeof(cmd), "G0 Y%.1f",
                          static_cast<double>(m_3dProbeRetract));
            m_cnc->sendCommand(cmd);
            break;
        case 3: // Center X (simplified)
            std::snprintf(cmd, sizeof(cmd), "G38.2 X%.0f F%.0f",
                          static_cast<double>(m_3dProbeSearchDist),
                          static_cast<double>(m_3dProbeSpeed));
            m_cnc->sendCommand(cmd);
            std::snprintf(cmd, sizeof(cmd), "G0 X-%.1f",
                          static_cast<double>(m_3dProbeRetract));
            m_cnc->sendCommand(cmd);
            // Note: Full center-finding requires reading probe positions
            // from status reports between probes. This simplified version
            // probes one direction and zeros.
            m_cnc->sendCommand("G10 L20 P0 X0");
            break;
        }
        m_cnc->sendCommand("G90");
    }
    if (!canProbe) ImGui::EndDisabled();

    if (!canProbe) {
        ImGui::TextColored(colors::kOrange,
                           "Machine must be Idle and connected to probe");
    }
}

void CncSafetyPanel::onStatusUpdate(const MachineStatus& status) {
    m_status = status;
    m_doorActive = (status.inputPins & cnc::PIN_DOOR) != 0 ||
                   status.state == MachineState::Door;
}

bool CncSafetyPanel::isDoorInterlockActive() const {
    return m_doorActive && Config::instance().getSafetyDoorInterlockEnabled();
}

void CncSafetyPanel::onConnectionChanged(bool connected, const std::string& /*version*/) {
    m_connected = connected;
    if (!connected) {
        m_streaming = false;
        m_status = MachineStatus{};
    }
}

} // namespace dw
