// Final review view for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <imgui.h>

#include "core/carve/carve_job.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {

constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kRed = colors::kError;
constexpr auto& kYellow = colors::kWarning;

} // namespace

void DirectCarvePanel::renderCommit() {
    ImGui::TextUnformatted("Review & Run");
    ImGui::Spacing();

    if (ImGui::Checkbox("The stock is clamped securely and the work area is clear",
                        &m_stockSecuredConfirmed)) {
        clearFinalConfirmation();
    }
    ImGui::TextDisabled(
        "This is a required preflight check; a preview cannot verify your clamps.");
    ImGui::Spacing();
    ImGui::TextWrapped("Review the carve job parameters before starting:");
    ImGui::Spacing();
    auto state = workflowState();
    const auto missingBeforeFinal =
        carve::missingDirectCarveRequirements(state, false);

    ImGui::BulletText("Machine: %s", m_cncConnected ? "Connected" : "DISCONNECTED");
    ImGui::BulletText("Stock: %.0f x %.0f x %.0f mm",
                      static_cast<double>(m_stock.width),
                      static_cast<double>(m_stock.height),
                      static_cast<double>(m_stock.thickness));
    ImGui::BulletText("Feed: %.0f mm/min, Plunge: %.0f mm/min",
                      static_cast<double>(m_toolpathConfig.feedRateMmMin),
                      static_cast<double>(m_toolpathConfig.plungeRateMmMin));
    ImGui::BulletText("Safe Z: %.1f mm", static_cast<double>(m_toolpathConfig.safeZMm));
    const auto& effectiveClearingTool = m_toolPlan.effectiveClearingTool();
    if (hasCurrentToolpath() && effectiveClearingTool) {
        ImGui::BulletText("Roughing tool: %s",
                          resolveToolNameFormat(*effectiveClearingTool).c_str());
    } else if (!m_autoRoughingWarning.empty()) {
        ImGui::BulletText("Roughing: %s", m_autoRoughingWarning.c_str());
    }
    const auto& finishingTool = m_toolPlan.finishingIntent();
    if (finishingTool) {
        ImGui::BulletText("Finish tool: %s",
                          resolveToolNameFormat(*finishingTool).c_str());
    }
    const auto units = detectedSendUnits();
    ImGui::BulletText("Send units: %s (%s)",
                      cnc::unitLabel(units),
                      cnc::gcodeUnitMode(units));

    if (hasCurrentToolpath()) {
        const auto& tp = m_carveJob->toolpath();
        ImGui::BulletText("Estimated time: %s", formatTime(tp.totalTimeSec).c_str());
        ImGui::BulletText("G-code lines: %d", tp.totalLineCount);
        if (tp.requiresToolChange) {
            ImGui::BulletText("Tool change pause: enabled before raster");
        }
    }

    ImGui::Spacing();

    ImGui::SeparatorText("Requirements");
    if (missingBeforeFinal.empty() && m_stockSecuredConfirmed && m_runEffectExecutor) {
        ImGui::TextColored(kGreen, "All workflow requirements are satisfied.");
    } else {
        ImGui::TextColored(kRed, "Start Carving is locked. Missing:");
        for (const auto requirement : missingBeforeFinal) {
            ImGui::BulletText("%s",
                carve::directCarveRequirementLabel(requirement));
        }
        if (!m_stockSecuredConfirmed)
            ImGui::BulletText("Stock clamped and work area clear");
        if (!m_runEffectExecutor)
            ImGui::BulletText("Protected Run service available");
    }
    ImGui::Spacing();

    // G-code export button
    float bw = ImGui::GetFontSize() * 10.0f;
    if (ImGui::Button("Save as G-code", ImVec2(bw, 0)))
        saveGCodeToProject();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
    ImGui::TextWrapped("This will begin streaming G-code to the machine. "
                       "Ensure the work area is clear and the spindle is ready.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    const bool preflightIncomplete = !missingBeforeFinal.empty() ||
                                     !m_stockSecuredConfirmed ||
                                     !m_runEffectExecutor;
    if (preflightIncomplete) ImGui::BeginDisabled();
    if (ImGui::Checkbox("I confirm the above and am ready to carve", &m_commitConfirmed)) {
        if (m_commitConfirmed) {
            m_commitConfirmedSettingsVersion = m_settingsVersion;
            m_commitConfirmedToolpathVersion = m_generatedAtVersion;
        } else {
            clearFinalConfirmation();
        }
    }
    if (preflightIncomplete) {
        ImGui::EndDisabled();
        clearFinalConfirmation();
    }
}

} // namespace dw
