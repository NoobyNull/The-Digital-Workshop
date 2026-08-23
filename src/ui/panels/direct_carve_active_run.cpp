// Active-run monitoring and controls for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "ui/ui_colors.h"

namespace dw {

namespace {

constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kRed = colors::kError;
constexpr auto& kYellow = colors::kWarning;
constexpr auto& kDimmed = colors::kDimmed;

void centeredProgressBar(float fraction, const ImVec2& size, const char* overlay) {
    ImGui::ProgressBar(fraction, size, "");
    if (overlay && overlay[0]) {
        ImVec2 textSize = ImGui::CalcTextSize(overlay);
        ImVec2 barMin = ImGui::GetItemRectMin();
        ImVec2 barMax = ImGui::GetItemRectMax();
        float cx = barMin.x + (barMax.x - barMin.x - textSize.x) * 0.5f;
        float cy = barMin.y + (barMax.y - barMin.y - textSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(cx, cy), IM_COL32(255, 255, 255, 255), overlay);
    }
}

} // namespace

void DirectCarvePanel::renderRunning() {
    float fontSize = ImGui::GetFontSize();
    float bw = fontSize * 8.0f;

    // State label
    const char* stateLabel = "Unknown";
    ImVec4 stateColor = kDimmed;
    const auto runState = m_runCoordinator.snapshot().state;
    switch (runState) {
    case run_coordination::RunState::Idle:
        stateLabel = "Not started";
        break;
    case run_coordination::RunState::Streaming:
        stateLabel = "Streaming";
        stateColor = kGreen;
        break;
    case run_coordination::RunState::Paused:
        stateLabel = "Paused (Feed Hold)";
        stateColor = kYellow;
        break;
    case run_coordination::RunState::Completed:
        stateLabel = "Complete";
        stateColor = kGreen;
        break;
    case run_coordination::RunState::Aborted:
        stateLabel = "Aborted";
        stateColor = kRed;
        break;
    case run_coordination::RunState::Failed:
        stateLabel = "Failed";
        stateColor = kRed;
        break;
    }

    ImGui::TextColored(stateColor, "%s", stateLabel);
    ImGui::Spacing();

    // Progress bar
    if (m_runTotalLines > 0) {
        float fraction = static_cast<float>(m_runCurrentLine) /
                         static_cast<float>(m_runTotalLines);
        float etaSec = 0.0f;
        if (m_runCurrentLine > 0 && fraction < 1.0f && m_runElapsedSec > 0.0f) {
            float rate = static_cast<float>(m_runCurrentLine) / m_runElapsedSec;
            float remaining = static_cast<float>(m_runTotalLines - m_runCurrentLine);
            etaSec = remaining / rate;
        }
        char overlay[128];
        int etaMin = static_cast<int>(etaSec / 60.0f);
        int etaS = static_cast<int>(etaSec) % 60;
        std::snprintf(overlay, sizeof(overlay), "Line %d / %d  (%.0f%%)  ETA: %d:%02d",
                      m_runCurrentLine, m_runTotalLines,
                      static_cast<double>(fraction * 100.0f), etaMin, etaS);
        centeredProgressBar(fraction, ImVec2(-1, 0), overlay);
    }

    // Pass indicator and elapsed time
    if (!m_runCurrentPass.empty())
        ImGui::Text("Pass: %s", m_runCurrentPass.c_str());
    ImGui::Text("Elapsed: %s", formatTime(m_runElapsedSec).c_str());

    // Machine position
    ImGui::Text("Position: X%.3f Y%.3f Z%.3f",
                static_cast<double>(m_machineStatus.workPos.x),
                static_cast<double>(m_machineStatus.workPos.y),
                static_cast<double>(m_machineStatus.workPos.z));
    ImGui::Spacing();

    // Control buttons (active job only)
    if (runState == run_coordination::RunState::Streaming ||
        runState == run_coordination::RunState::Paused) {
#ifdef DW_ENABLE_UX_CAPTURE
        if (m_uxCapturePrimeAbortFocus && !m_uxCaptureFocusPrimed) {
            ImGui::SetKeyboardFocusHere();
        }
#endif
        // Pause / Resume
        if (runState == run_coordination::RunState::Streaming) {
            if (ImGui::Button("Pause", ImVec2(bw, 0))) {
                requestRunPause();
            }
        } else {
            if (ImGui::Button("Resume", ImVec2(bw, 0))) {
                requestRunResume();
            }
        }
#ifdef DW_ENABLE_UX_CAPTURE
        if (m_uxCapturePrimeAbortFocus && ImGui::IsItemFocused()) {
            m_uxCaptureFocusPrimed = true;
        }
#endif

        // Abort — long-press for safety
        ImGui::SameLine();
        ImGui::Button("Hold to Abort", ImVec2(bw * 1.2f, 0));
#ifdef DW_ENABLE_UX_CAPTURE
        if (m_uxCapturePrimeAbortFocus && ImGui::IsItemFocused()) {
            m_uxCaptureAbortFocused = true;
        }
#endif
        bool isHeld = ImGui::IsItemActive();
        float requiredMs = 1500.0f;
        if (isHeld) {
            m_abortHoldTime += ImGui::GetIO().DeltaTime * 1000.0f;
            float progress = std::min(m_abortHoldTime / requiredMs, 1.0f);
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 fillMax = {rmin.x + (rmax.x - rmin.x) * progress, rmax.y};
            ImGui::GetWindowDrawList()->AddRectFilled(
                rmin, fillMax, IM_COL32(255, 80, 80, 60), 3.0f);
            m_abortHolding = true;
            if (m_abortHoldTime >= requiredMs) {
                requestRunAbort();
                m_abortHoldTime = 0.0f;
                m_abortHolding = false;
            }
        } else {
            if (m_abortHolding) { m_abortHolding = false; m_abortHoldTime = 0.0f; }
        }
    }

    // Post-completion / post-abort UI
    if (runState == run_coordination::RunState::Completed) {
        ImGui::Spacing();
        ImGui::TextColored(kGreen, "Carve completed successfully.");
        ImGui::TextWrapped(
            "The completed run is recorded in Project history. Use Back to Project above to continue.");
        if (ImGui::Button("Save G-code", ImVec2(bw, 0)))
            saveGCodeToProject();
    }

    if (runState == run_coordination::RunState::Aborted ||
        runState == run_coordination::RunState::Failed) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("Job stopped. Tool may be in workpiece -- "
                           "jog Z up before moving XY.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Save G-code", ImVec2(bw, 0)))
            saveGCodeToProject();
    }
}

} // namespace dw
