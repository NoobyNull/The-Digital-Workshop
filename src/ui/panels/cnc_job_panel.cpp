#include "ui/panels/cnc_job_panel.h"

#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "core/config/config.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/ui_colors.h"
#include "ui/widgets/table_helpers.h"

namespace dw {

CncJobPanel::CncJobPanel() : Panel("Job Progress") {}

void CncJobPanel::render() {
    if (!m_open)
        return;

    applyMinSize(14, 8);
    if (!ImGui::Begin(m_title.c_str(), closeHandle())) {
        ImGui::End();
        return;
    }

    if (!m_streaming && m_progress.totalLines == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("No active job");
        ImGui::TextDisabled("Load and stream G-code to see progress");
        ImGui::End();
        return;
    }

    renderProgressBar();
    ImGui::Spacing();
    renderLineInfo();
    ImGui::Spacing();
    renderTimeInfo();
    ImGui::Spacing();
    renderFeedDeviation();

    ImGui::End();
}

void CncJobPanel::renderProgressBar() {
    if (m_progress.totalLines <= 0)
        return;

    float fraction = static_cast<float>(m_progress.ackedLines) /
                     static_cast<float>(m_progress.totalLines);

    // Progress bar with percentage overlay
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.1f%%",
                  static_cast<double>(fraction * 100.0f));
    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);

    // Color-coded percentage text below bar
    ImVec4 pctColor;
    if (fraction >= 0.75f)
        pctColor = colors::kSuccess; // Green
    else if (fraction >= 0.25f)
        pctColor = colors::kWarning; // Yellow
    else
        pctColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // White/gray

    ImGui::TextColored(pctColor, "%.1f%% complete",
                       static_cast<double>(fraction * 100.0f));
}

void CncJobPanel::renderLineInfo() {
    ImGui::SeparatorText("Progress");

    if (auto t = LabelValueTable("##jobprogress")) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d / %d", m_progress.ackedLines, m_progress.totalLines);
        t.row("Line:", buf);
    }

    if (m_progress.errorCount > 0) {
        ImGui::TextColored(colors::kError,
                           "%s Errors: %d", Icons::Error, m_progress.errorCount);
    }
}

void CncJobPanel::renderTimeInfo() {
    ImGui::SeparatorText("Time");

    if (auto t = LabelValueTable("##jobtime")) {
        // Elapsed time
        std::string elapsedStr = formatTime(m_progress.elapsedSeconds);
        t.row("Elapsed:", elapsedStr.c_str());

        // Remaining time estimate
        if (m_progress.ackedLines < 5 || m_progress.elapsedSeconds < 3.0f) {
            t.rowDisabled("Remaining:", "Calculating...");
        } else if (m_progress.ackedLines >= m_progress.totalLines) {
            t.rowColored("Remaining:", ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Complete");
        } else {
            float rate = static_cast<float>(m_progress.ackedLines) / m_progress.elapsedSeconds;
            float remaining = static_cast<float>(m_progress.totalLines - m_progress.ackedLines);
            float eta = remaining / rate;
            std::string etaStr = formatTime(eta);
            t.rowf("Remaining:", "~%s", etaStr.c_str());
        }
    }
}

void CncJobPanel::renderFeedDeviation() {
    if (m_recommendedFeedRate <= 0.0f) {
        ImGui::Spacing();
        ImGui::TextDisabled("Select tool + material for feed comparison");
        return;
    }

    ImGui::SeparatorText("Feed Rate");

    auto& cfg = Config::instance();
    bool metric = cfg.getDisplayUnitsMetric();
    float uf = metric ? 1.0f : (1.0f / 25.4f);
    const char* fu = metric ? "mm/min" : "in/min";

    // Show actual vs recommended
    float actual = m_status.feedRate;
    float recommended = m_recommendedFeedRate;
    float effectiveRecommended = recommended *
        (static_cast<float>(m_status.feedOverride) / 100.0f);

    if (auto t = LabelValueTable("##feeddeviation")) {
        t.rowf("Actual:", "%.0f %s", static_cast<double>(actual * uf), fu);
        t.rowf("Recommended:", "%.0f %s", static_cast<double>(recommended * uf), fu);

        // Show override-adjusted if override differs from 100%
        if (m_status.feedOverride != 100) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.0f %s (%d%% override)",
                static_cast<double>(effectiveRecommended * uf), fu, m_status.feedOverride);
            t.rowDisabled("Adjusted:", buf);
        }
    }

    // Deviation display — only meaningful during Run state while streaming
    if (m_streaming && m_status.state == MachineState::Run && actual > 0.0f) {
        float deviationPct = m_feedDeviation * 100.0f;

        if (m_feedDeviationWarning) {
            // Red warning for >20% deviation
            ImGui::Spacing();
            ImVec4 warningColor = colors::kError;
            ImGui::TextColored(warningColor, "%s Feed Deviation: %.0f%%",
                Icons::Warning, static_cast<double>(deviationPct));

            // Red background highlight for visibility
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            min.x -= 4.0f; min.y -= 2.0f;
            max.x += 4.0f; max.y += 2.0f;
            ImGui::GetWindowDrawList()->AddRectFilled(
                min, max, IM_COL32(255, 60, 60, 40), 3.0f);
        } else {
            // Normal — green indicator
            ImGui::TextColored(colors::kSuccess,
                "Deviation: %.0f%% (OK)", static_cast<double>(deviationPct));
        }
    } else if (!m_streaming) {
        ImGui::TextDisabled("Start a job to see feed comparison");
    }
}

void CncJobPanel::onStatusUpdate(const MachineStatus& status) {
    m_status = status;

    // Feed deviation check — only meaningful during Run state while streaming
    if (m_streaming && m_recommendedFeedRate > 0.0f &&
        status.state == MachineState::Run && status.feedRate > 0.0f) {
        // Account for feed override: if user set 50% override,
        // the expected feed is 50% of recommended
        float effectiveRecommended = m_recommendedFeedRate *
            (static_cast<float>(status.feedOverride) / 100.0f);

        if (effectiveRecommended > 0.0f) {
            m_feedDeviation = std::abs(status.feedRate - effectiveRecommended) / effectiveRecommended;
            m_feedDeviationWarning = (m_feedDeviation > 0.20f);
        }
    } else {
        // Not in a state where deviation is meaningful
        m_feedDeviation = 0.0f;
        m_feedDeviationWarning = false;
    }
}

void CncJobPanel::onProgressUpdate(const StreamProgress& progress) {
    m_progress = progress;
}

void CncJobPanel::setStreaming(bool streaming) {
    m_streaming = streaming;
    if (!streaming) {
        // Clear deviation state when streaming stops
        m_feedDeviation = 0.0f;
        m_feedDeviationWarning = false;
    }
}

std::string CncJobPanel::formatTime(float seconds) {
    int totalSec = static_cast<int>(seconds);
    int h = totalSec / 3600;
    int m = (totalSec / 60) % 60;
    int s = totalSec % 60;

    char buf[32];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return std::string(buf);
}

} // namespace dw
