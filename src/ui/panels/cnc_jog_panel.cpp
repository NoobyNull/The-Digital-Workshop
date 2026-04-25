#include "ui/panels/cnc_jog_panel.h"

#include <cstdio>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "ui/icons.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {
struct LongPressButton {
    bool holding = false;
    float holdTime = 0.0f;

    // Returns true when hold completes. Call each frame.
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

static LongPressButton s_homeLongPress;
} // namespace

CncJogPanel::CncJogPanel() : Panel("Jog Control") {}

void CncJogPanel::render() {
    if (!m_open)
        return;

    applyMinSize(14, 10);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    if (!m_connected) {
        ImGui::Spacing();
        ImGui::TextColored(colors::kDimmed, "%s Disconnected", Icons::Unlink);
        ImGui::TextDisabled("Connect a CNC machine to jog");
        ImGui::End();
        return;
    }

    renderStepSizeSelector();
    ImGui::Spacing();
    renderJogButtons();
    ImGui::Spacing();
    renderHomingSection();

    ImGui::End();
}

void CncJogPanel::renderStepSizeSelector() {
    ImGui::SeparatorText("Step Size");

    auto& cfg = Config::instance();
    bool metric = cfg.getDisplayUnitsMetric();
    float uf = metric ? 1.0f : (1.0f / 25.4f);
    const char* pu = metric ? "mm" : "in";
    const char* fu = metric ? "mm/min" : "in/min";

    for (int i = 0; i < NUM_STEPS; ++i) {
        if (i > 0)
            ImGui::SameLine();
        if (metric) {
            if (ImGui::RadioButton(STEP_LABELS[i], m_selectedStep == i))
                m_selectedStep = i;
        } else {
            char inLabel[16];
            std::snprintf(inLabel, sizeof(inLabel), "%.4f", static_cast<double>(STEP_SIZES[i] * uf));
            if (ImGui::RadioButton(inLabel, m_selectedStep == i))
                m_selectedStep = i;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pu);
    ImGui::TextDisabled("Tab to cycle step groups");

    // Show active feedrate for the selected step group
    int feed;
    const char* group;
    if (m_selectedStep <= 1) {
        feed = cfg.getJogFeedSmall(); group = "small";
    } else if (m_selectedStep == 2) {
        feed = cfg.getJogFeedMedium(); group = "medium";
    } else {
        feed = cfg.getJogFeedLarge(); group = "large";
    }
    double feedRate = static_cast<double>(feed) * static_cast<double>(uf);
    ImGui::TextDisabled("Feed: %.0f %s (%s)", feedRate, fu, group);
}

void CncJogPanel::renderJogButtons() {
    ImGui::SeparatorText("Jog");

    bool canJog = m_cnc && (m_status.state == MachineState::Idle ||
                            m_status.state == MachineState::Jog);

    float jogPad = ImGui::GetFontSize() * 0.75f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(jogPad, jogPad));
    float buttonSize = ImGui::GetFontSize() * 3.0f;

    // Helper lambda for single-axis jog buttons
    auto jogButton = [&](const char* label, int axis, float dir) {
        if (!canJog)
            ImGui::BeginDisabled();
        if (ImGui::Button(label, ImVec2(buttonSize, buttonSize))) {
            jogAxis(axis, dir);
        }
        if (!canJog)
            ImGui::EndDisabled();
    };

    // Helper lambda for diagonal jog buttons
    auto diagButton = [&](const char* label, float xDir, float yDir, const char* tooltip) {
        if (!canJog)
            ImGui::BeginDisabled();
        if (ImGui::Button(label, ImVec2(buttonSize, buttonSize))) {
            jogDiagonal(xDir, yDir);
        }
        if (!canJog)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip);
        }
    };

    // Calculate centering offset for the 3x3 grid
    float availWidth = ImGui::GetContentRegionAvail().x;
    float crossWidth = buttonSize * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
    float offsetX = (availWidth - crossWidth) * 0.5f;
    if (offsetX < 0)
        offsetX = 0;

    // Row 1: -X+Y, Y+, +X+Y
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
    diagButton("\\##NW", -1.0f, +1.0f, "-X +Y");
    ImGui::SameLine();
    jogButton("Y+", 1, +1.0f);
    ImGui::SameLine();
    diagButton("/##NE", +1.0f, +1.0f, "+X +Y");

    // Row 2: X-, [gap], X+
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
    jogButton("X-", 0, -1.0f);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(buttonSize, buttonSize));
    ImGui::SameLine();
    jogButton("X+", 0, +1.0f);

    // Row 3: -X-Y, Y-, +X-Y
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
    diagButton("/##SW", -1.0f, -1.0f, "-X -Y");
    ImGui::SameLine();
    jogButton("Y-", 1, -1.0f);
    ImGui::SameLine();
    diagButton("\\##SE", +1.0f, -1.0f, "+X -Y");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Z axis — side by side
    float zGroupWidth = buttonSize * 2 + ImGui::GetStyle().ItemSpacing.x;
    float zOffsetX = (availWidth - zGroupWidth) * 0.5f;
    if (zOffsetX < 0)
        zOffsetX = 0;

    ImGui::TextDisabled("Z Axis");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + zOffsetX);
    jogButton("Z+", 2, +1.0f);
    ImGui::SameLine();
    jogButton("Z-", 2, -1.0f);

    ImGui::PopStyleVar(); // FramePadding
}

void CncJogPanel::renderHomingSection() {
    ImGui::SeparatorText("Home / Unlock");

    // Home button — enabled when Idle or Alarm
    bool canHome = m_cnc && (m_status.state == MachineState::Idle ||
                              m_status.state == MachineState::Alarm);

    auto& cfg = Config::instance();
    bool useLongPress = cfg.getSafetyLongPressEnabled();

    if (useLongPress) {
        char homeLabel[64];
        std::snprintf(homeLabel, sizeof(homeLabel), "%s Hold to Home", Icons::Home);
        float durationMs = static_cast<float>(cfg.getSafetyLongPressDurationMs());
        float homeBtnW = ImGui::CalcTextSize(homeLabel).x + ImGui::GetStyle().FramePadding.x * 2;
        if (s_homeLongPress.render(homeLabel, ImVec2(homeBtnW, 0), durationMs, canHome)) {
            if (m_cnc)
                m_cnc->sendCommand("$H");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Hold button for %.1fs to home", static_cast<double>(durationMs / 1000.0f));
        }
    } else {
        if (!canHome)
            ImGui::BeginDisabled();
        char homeLabel[64];
        std::snprintf(homeLabel, sizeof(homeLabel), "%s Home All", Icons::Home);
        float homeBtnW = ImGui::CalcTextSize(homeLabel).x + ImGui::GetStyle().FramePadding.x * 2;
        if (ImGui::Button(homeLabel, ImVec2(homeBtnW, 0))) {
            if (m_cnc)
                m_cnc->sendCommand("$H");
        }
        if (!canHome)
            ImGui::EndDisabled();
    }

    ImGui::SameLine();

    // Unlock button — only enabled in Alarm state
    bool canUnlock = m_cnc && m_status.state == MachineState::Alarm;
    if (!canUnlock)
        ImGui::BeginDisabled();
    char unlockLabel[64];
    std::snprintf(unlockLabel, sizeof(unlockLabel), "%s Unlock", Icons::LockOpen);
    float unlockBtnW = ImGui::CalcTextSize(unlockLabel).x + ImGui::GetStyle().FramePadding.x * 2;
    if (ImGui::Button(unlockLabel, ImVec2(unlockBtnW, 0))) {
        if (m_cnc)
            m_cnc->unlock();
    }
    if (!canUnlock)
        ImGui::EndDisabled();

    if (m_status.state == MachineState::Alarm) {
        ImGui::TextColored(colors::kError,
                            "Machine is in ALARM state. Home or Unlock to clear.");
    }
}

void CncJogPanel::jogAxis(int axis, float direction) {
    if (!m_cnc)
        return;
    static const char axisLetters[] = {'X', 'Y', 'Z'};
    if (axis < 0 || axis > 2)
        return;

    float step = STEP_SIZES[m_selectedStep] * direction;

    // Per-step-group feedrate from Config
    auto& cfg = Config::instance();
    float feed;
    if (m_selectedStep <= 1) {
        // Small group: 0.01mm, 0.1mm
        feed = static_cast<float>(cfg.getJogFeedSmall());
    } else if (m_selectedStep == 2) {
        // Medium group: 1mm
        feed = static_cast<float>(cfg.getJogFeedMedium());
    } else {
        // Large group: 10mm, 100mm
        feed = static_cast<float>(cfg.getJogFeedLarge());
    }

    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "$J=G91 G21 %c%.3f F%.0f",
                  axisLetters[axis], static_cast<double>(step), static_cast<double>(feed));
    m_cnc->sendCommand(cmd);
}

void CncJogPanel::jogDiagonal(float xDir, float yDir) {
    if (!m_cnc)
        return;

    float step = STEP_SIZES[m_selectedStep];
    float xStep = step * xDir;
    float yStep = step * yDir;

    // Per-step-group feedrate from Config (same logic as jogAxis)
    auto& cfg = Config::instance();
    float feed;
    if (m_selectedStep <= 1) {
        feed = static_cast<float>(cfg.getJogFeedSmall());
    } else if (m_selectedStep == 2) {
        feed = static_cast<float>(cfg.getJogFeedMedium());
    } else {
        feed = static_cast<float>(cfg.getJogFeedLarge());
    }

    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "$J=G91 G21 X%.3f Y%.3f F%.0f",
                  static_cast<double>(xStep), static_cast<double>(yStep),
                  static_cast<double>(feed));
    m_cnc->sendCommand(cmd);
}

void CncJogPanel::startContinuousJog(int axis, float direction, int key) {
    if (!m_cnc)
        return;
    static const char axisLetters[] = {'X', 'Y', 'Z'};
    if (axis < 0 || axis > 2)
        return;

    float dist = CONTINUOUS_JOG_DISTANCE * direction;
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "$J=G91 G21 %c%.0f F%.0f",
                  axisLetters[axis], static_cast<double>(dist),
                  static_cast<double>(CONTINUOUS_JOG_FEED));
    m_cnc->sendCommand(cmd);
    m_contJogAxis = axis;
    m_contJogDir = direction;
    m_contJogKey = key;
    m_jogWatchdogTimer = 0.0f;
}

void CncJogPanel::stopContinuousJog() {
    if (m_cnc) {
        m_cnc->jogCancel();
    }
    m_contJogAxis = -1;
    m_contJogKey = 0;
    m_jogWatchdogTimer = 0.0f;
}

void CncJogPanel::cycleStepGroup() {
    // Step groups: Small (0,1) = 0.01-0.1, Medium (2) = 1, Large (3,4) = 10-100
    // Cycle: Small -> Medium -> Large -> Small
    if (m_selectedStep <= 1) {
        // Currently in Small -> go to Medium
        m_selectedStep = 2;
    } else if (m_selectedStep == 2) {
        // Currently in Medium -> go to Large
        m_selectedStep = 3;
    } else {
        // Currently in Large -> go to Small
        m_selectedStep = 1; // 0.1mm (more useful default than 0.01mm)
    }
}

void CncJogPanel::handleKeyboardJog() {
    if (!m_cnc || !m_connected)
        return;

    // Tab key cycles step groups (only when no text input is focused)
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ImGui::GetIO().WantTextInput) {
        cycleStepGroup();
    }

    // Check if continuous jog should stop (key released or watchdog timeout)
    if (m_contJogAxis >= 0) {
        if (!ImGui::IsKeyDown(static_cast<ImGuiKey>(m_contJogKey))) {
            stopContinuousJog();
        } else {
            // Key appears held — reset watchdog (positive keepalive)
            m_jogWatchdogTimer = 0.0f;
        }
        // Dead-man watchdog: catches stale key state (focus loss, etc.)
        // Timer increments every frame; resets above when key confirmed down.
        // If no reset arrives within timeout, force stop.
        if (m_contJogAxis >= 0) {
            auto& cfg = Config::instance();
            if (cfg.getSafetyDeadManEnabled()) {
                m_jogWatchdogTimer += ImGui::GetIO().DeltaTime * 1000.0f;
                float timeout = static_cast<float>(cfg.getSafetyDeadManTimeoutMs());
                if (m_jogWatchdogTimer >= timeout) {
                    stopContinuousJog();
                }
            }
        }
        return; // Don't start new jog while continuous is active
    }

    // Check if machine state allows jogging
    if (m_status.state != MachineState::Idle && m_status.state != MachineState::Jog)
        return;

    // Key mapping: arrows for X/Y, page up/down for Z
    struct JogKey {
        ImGuiKey key;
        int axis;
        float dir;
    };
    static const JogKey keys[] = {
        {ImGuiKey_RightArrow, 0, +1.0f}, // X+
        {ImGuiKey_LeftArrow, 0, -1.0f},  // X-
        {ImGuiKey_UpArrow, 1, +1.0f},    // Y+
        {ImGuiKey_DownArrow, 1, -1.0f},  // Y-
        {ImGuiKey_PageUp, 2, +1.0f},     // Z+
        {ImGuiKey_PageDown, 2, -1.0f},   // Z-
    };

    for (const auto& k : keys) {
        if (ImGui::IsKeyPressed(k.key, false)) {
            // If Shift held, start continuous jog
            if (ImGui::GetIO().KeyShift) {
                startContinuousJog(k.axis, k.dir, static_cast<int>(k.key));
            } else {
                jogAxis(k.axis, k.dir);
            }
        }
    }
}

void CncJogPanel::onStatusUpdate(const MachineStatus& status) {
    m_status = status;
}

void CncJogPanel::onConnectionChanged(bool connected, const std::string& /*version*/) {
    m_connected = connected;
    if (!connected) {
        m_status = MachineStatus{};
        stopContinuousJog();
    }
}

} // namespace dw
