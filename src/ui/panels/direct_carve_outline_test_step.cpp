// Safe-Z outline-test view for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
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

void DirectCarvePanel::renderOutlineTest() {
    ImGui::TextUnformatted("Machine Setup — Outline Check");
    ImGui::Spacing();
    ImGui::TextWrapped("Traces the job perimeter at safe Z to verify work area.");
    ImGui::Spacing();

    if (!hasCurrentToolpath()) {
        ImGui::TextColored(kYellow, "Generate a toolpath first.");
        return;
    }

    const auto& tp = m_carveJob->toolpath();
    f32 minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    auto includeCutBounds = [&](const carve::Toolpath& path) {
        for (const auto& pt : path.points) {
            if (!pt.rapid) {
                minX = std::min(minX, pt.position.x);
                maxX = std::max(maxX, pt.position.x);
                minY = std::min(minY, pt.position.y);
                maxY = std::max(maxY, pt.position.y);
            }
        }
    };
    includeCutBounds(tp.clearing);
    includeCutBounds(tp.finishing);

    ImGui::Text("Bounding box: X[%.1f .. %.1f]  Y[%.1f .. %.1f]",
                static_cast<double>(minX), static_cast<double>(maxX),
                static_cast<double>(minY), static_cast<double>(maxY));
    ImGui::Text("Size: %.1f x %.1f mm",
                static_cast<double>(maxX - minX), static_cast<double>(maxY - minY));
    ImGui::Spacing();

    float bw = ImGui::GetFontSize() * 10.0f;

    if (!m_outlineCompleted && !m_outlineRunning) {
        if (ImGui::Button("Run Outline", ImVec2(bw, 0))) {
            if (m_cnc && m_cncConnected) {
                f32 safeZ = m_toolpathConfig.safeZMm;
                char cmd[128];
                std::snprintf(cmd, sizeof(cmd), "G90 G0 Z%.3f", static_cast<double>(safeZ));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(minX), static_cast<double>(minY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(maxX), static_cast<double>(minY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(maxX), static_cast<double>(maxY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(minX), static_cast<double>(maxY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(minX), static_cast<double>(minY));
                m_cnc->sendCommand(cmd);
                m_outlineCompleted = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Skip Outline", &m_outlineSkipped))
            clearFinalConfirmation();
    }

    if (m_outlineCompleted)
        ImGui::TextColored(kGreen, "Outline complete -- verify work area before proceeding.");
    if (m_outlineSkipped && !m_outlineCompleted)
        ImGui::TextColored(kYellow, "Outline test skipped.");
}

} // namespace dw
