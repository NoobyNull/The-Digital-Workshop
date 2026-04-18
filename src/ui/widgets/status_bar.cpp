// Digital Workshop - Status Bar Widget Implementation

#include "ui/widgets/status_bar.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/import/import_task.h"
#include "core/threading/loading_state.h"

namespace dw {

void StatusBar::render(const LoadingState* loadingState) {
    auto* viewport = ImGui::GetMainViewport();
    float barHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;

    ImVec2 pos(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - barHeight);
    ImVec2 size(viewport->WorkSize.x, barHeight);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().FramePadding.y));
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        // Left side: loading status or "Ready"
        if (loadingState && loadingState->active.load()) {
            int dots = (static_cast<int>(ImGui::GetTime() * 3.0) % 3) + 1;
            std::string dotsStr(static_cast<size_t>(dots), '.');
            ImGui::Text("Loading %s%s", loadingState->getName().c_str(), dotsStr.c_str());
        } else if (!m_progress || !m_progress->active.load()) {
            ImGui::TextDisabled("Ready");
        }

        // Right side: import progress if active
        if (m_progress && m_progress->active.load()) {
            // Show import progress
            int completed = m_progress->completedFiles.load();
            int total = m_progress->totalFiles.load();
            int failed = m_progress->failedFiles.load();

            // Left: current file and stage
            std::string currentFile = m_progress->getCurrentFileName();
            if (!currentFile.empty()) {
                ImGui::Text("%s", currentFile.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("- %s", importStageName(m_progress->currentStage.load()));
            }

            // Align to right side
            float progressBarWidth = std::max(ImGui::GetContentRegionAvail().x * 0.15f,
                                              ImGui::GetFontSize() * 9.0f);
            float cancelButtonWidth = ImGui::CalcTextSize("X").x +
                                      ImGui::GetStyle().FramePadding.x * 2;
            float itemSpacing = ImGui::GetStyle().ItemSpacing.x * 2;
            float totalWidth = progressBarWidth + cancelButtonWidth + itemSpacing;
            float windowWidth = ImGui::GetWindowWidth();
            float minContentWidth = totalWidth + windowWidth * 0.1f;
            if (windowWidth > minContentWidth) { // Ensure enough space
                ImGui::SameLine(windowWidth - totalWidth - ImGui::GetStyle().WindowPadding.x);
            } else {
                ImGui::SameLine();
            }

            // Progress bar
            float fraction = total > 0 ? static_cast<float>(completed) / static_cast<float>(total)
                                       : 0.0f;
            char overlay[64];
            std::snprintf(overlay, sizeof(overlay), "%d/%d", completed, total);
            ImGui::ProgressBar(fraction, ImVec2(progressBarWidth, 0), overlay);

            // Cancel button
            ImGui::SameLine();
            if (ImGui::SmallButton("X") && m_onCancel) {
                m_onCancel();
            }

            // Show failed count if > 0 (in tooltip to save space)
            if (failed > 0 && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%d files failed", failed);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void StatusBar::setImportProgress(const ImportProgress* progress) {
    m_progress = progress;
}

void StatusBar::clearImportProgress() {
    m_progress = nullptr;
}

} // namespace dw
