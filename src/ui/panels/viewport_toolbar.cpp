#include "viewport_panel.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>

#include "../../core/config/config.h"
#include "../icons.h"
#include "../ui_colors.h"

namespace dw {

void ViewportPanel::renderToolbar() {
    auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(style.ItemSpacing.y, style.ItemSpacing.y));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(style.FramePadding.x, style.FramePadding.y));

    if (ImGui::Button(Icons::Refresh)) {
        resetView();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset View");
    ImGui::SameLine();

    if (ImGui::Button(Icons::Fit)) {
        fitToModel();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fit to Model");
    ImGui::SameLine();

    char rotateUpLabel[32];
    std::snprintf(rotateUpLabel,
                  sizeof(rotateUpLabel),
                  "%s##TiltViewUp90",
                  Icons::ArrowUp);
    if (ImGui::Button(rotateUpLabel, ImVec2(ImGui::GetFrameHeight(), 0))) {
        tiltViewByQuarterTurns(1);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rotate current view 90 degrees up");
    ImGui::SameLine();

    char rotateDownLabel[32];
    std::snprintf(rotateDownLabel,
                  sizeof(rotateDownLabel),
                  "%s##TiltViewDown90",
                  Icons::ArrowDown);
    if (ImGui::Button(rotateDownLabel, ImVec2(ImGui::GetFrameHeight(), 0))) {
        tiltViewByQuarterTurns(-1);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rotate current view 90 degrees down");
    ImGui::SameLine();

    char rotateLeftLabel[32];
    std::snprintf(rotateLeftLabel,
                  sizeof(rotateLeftLabel),
                  "%s##RotateViewLeft90",
                  Icons::ArrowLeft);
    if (ImGui::Button(rotateLeftLabel, ImVec2(ImGui::GetFrameHeight(), 0))) {
        rotateViewByQuarterTurns(1);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rotate current view 90 degrees left");
    ImGui::SameLine();

    char rotateRightLabel[32];
    std::snprintf(rotateRightLabel,
                  sizeof(rotateRightLabel),
                  "%s##RotateViewRight90",
                  Icons::ArrowRight);
    if (ImGui::Button(rotateRightLabel, ImVec2(ImGui::GetFrameHeight(), 0))) {
        rotateViewByQuarterTurns(-1);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rotate current view 90 degrees right");
    ImGui::SameLine();

    auto& cfg = Config::instance();
    NavStyle navStyle = cfg.getNavStyle();
    char navButtonLabel[16];
    std::snprintf(navButtonLabel,
                  sizeof(navButtonLabel),
                  "%s##NavStyleCycle",
                  navStyleLetter(navStyle));
    if (ImGui::Button(navButtonLabel, ImVec2(ImGui::GetFrameHeight(), 0))) {
        cfg.setNavStyle(nextNavStyle(navStyle));
        cfg.save();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Navigation: %s\n%s\nClick to cycle D/C/M",
                          navStyleName(navStyle),
                          navStyleControlHint(navStyle));
    }
    ImGui::SameLine();

    ImGui::Separator();
    ImGui::SameLine();

    // Wrap Wireframe checkbox to new line if too narrow
    if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 6.0f) {
        ImGui::NewLine();
    }

    bool wireframe = m_renderer.settings().wireframe;
    if (ImGui::Checkbox("Wireframe", &wireframe)) {
        m_renderer.settings().wireframe = wireframe;
    }

    // Master visibility toggles (always visible)
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::Checkbox("Model", &m_showModel);
    if (m_gpuToolpath.vao != 0) {
        ImGui::SameLine();
        ImGui::Checkbox("Toolpath", &m_showToolpath);
    }
    if (hasGCode()) {
        ImGui::SameLine();
        if (ImGui::Checkbox("G-code", &m_showGCode))
            m_gcodeDirty = true;
    }

    // Alignment status indicator (only when FitParams are active)
    if (m_hasFitParams && m_alignmentStatus != AlignmentStatus::Unknown) {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        if (m_alignmentStatus == AlignmentStatus::Aligned) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kSuccess);
            ImGui::Text("Aligned");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kError);
            ImGui::Text("Misaligned");
            ImGui::PopStyleColor();
        }
    }

    // G-code move-type toggles (only when G-code loaded + G-code visible)
    if (hasGCode() && m_showGCode) {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 22.0f) {
            ImGui::NewLine();
        }

        if (ImGui::Checkbox("Rapid", &m_showRapids))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Cut", &m_showCuts))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Plunge", &m_showPlunges))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Retract", &m_showRetracts))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Color by Tool", &m_colorByTool))
            m_gcodeDirty = true;

        // Z-clip slider
        ImGui::Text("Z clip:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderFloat(
                "##VPZClip", &m_zClipMax,
                m_gcodeProgram.boundsMin.z,
                m_zClipMaxBound, "%.2f mm")) {
            m_gcodeDirty = true;
        }

        // Simulation controls
        renderSimControls();
    }

    ImGui::PopStyleVar(2);
}


void ViewportPanel::renderSimControls() {
    if (!hasGCode() || m_segmentTimes.empty())
        return;

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Play / Pause toggle
    if (m_simState == VPSimState::Stopped || m_simState == VPSimState::Paused) {
        if (ImGui::Button("Play##Sim")) {
            m_simState = VPSimState::Playing;
        }
    } else {
        if (ImGui::Button("Pause##Sim")) {
            m_simState = VPSimState::Paused;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Stop##Sim")) {
        m_simState = VPSimState::Stopped;
        m_simTime = 0.0f;
        m_simSegmentIndex = 0;
        m_simSegmentProgress = 0.0f;
    }

    // Speed selector
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
    ImGui::Text("Speed:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x + ImGui::GetStyle().FramePadding.x * 2);
    static const float speeds[] = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    static const char* speedLabels[] = {"0.5x", "1x", "2x", "5x", "10x"};
    int currentSpeedIdx = 1;
    for (int i = 0; i < 5; ++i) {
        if (m_simSpeed == speeds[i])
            currentSpeedIdx = i;
    }
    if (ImGui::BeginCombo("##VPSimSpeed", speedLabels[currentSpeedIdx])) {
        for (int i = 0; i < 5; ++i) {
            if (ImGui::Selectable(speedLabels[i], i == currentSpeedIdx))
                m_simSpeed = speeds[i];
        }
        ImGui::EndCombo();
    }

    // Scrub slider
    ImGui::Text("Progress:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    float scrubFrac = (m_simTotalTime > 0.0f) ? (m_simTime / m_simTotalTime) : 0.0f;
    if (ImGui::SliderFloat("##VPSimScrub", &scrubFrac, 0.0f, 1.0f, "%.1f%%")) {
        m_simTime = scrubFrac * m_simTotalTime;
        // O(log n) binary search to find segment from time
        if (!m_segmentTimeCumulative.empty()) {
            auto it = std::lower_bound(m_segmentTimeCumulative.begin(),
                                        m_segmentTimeCumulative.end(), m_simTime);
            size_t idx = static_cast<size_t>(it - m_segmentTimeCumulative.begin());
            if (idx >= m_gcodeProgram.path.size())
                idx = m_gcodeProgram.path.size() - 1;
            float segStart = (idx > 0) ? m_segmentTimeCumulative[idx - 1] : 0.0f;
            float segDur = m_segmentTimes[idx];
            m_simSegmentIndex = idx;
            m_simSegmentProgress = (segDur > 0.0f) ? (m_simTime - segStart) / segDur : 0.0f;
        } else {
            m_simSegmentIndex = 0;
            m_simSegmentProgress = 0.0f;
        }
        // When scrubbing, ensure sim is in a visible state
        if (m_simState == VPSimState::Stopped)
            m_simState = VPSimState::Paused;
    }
}
} // namespace dw
