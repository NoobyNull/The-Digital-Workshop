// Digital Workshop - Import Summary Dialog Implementation

#include "ui/dialogs/import_summary_dialog.h"

#include <algorithm>

#include <imgui.h>

#include "core/utils/file_utils.h"
#include "ui/icons.h"
#include "ui/ui_colors.h"

namespace dw {

ImportSummaryDialog::ImportSummaryDialog() : Dialog("Import Complete") {}

void ImportSummaryDialog::open(const ImportBatchSummary& summary) {
    m_summary = summary;
    m_checked.assign(m_summary.duplicates.size(), true); // default: all checked
    m_open = true;
    m_pendingOpen = true;
}

void ImportSummaryDialog::setOnReimport(ReimportCallback callback) {
    m_onReimport = std::move(callback);
}

void ImportSummaryDialog::render() {
    if (!m_open)
        return;

    if (m_pendingOpen) {
        ImGui::OpenPopup(m_title.c_str());
        m_pendingOpen = false;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.4f, 0), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;

    if (ImGui::BeginPopupModal(m_title.c_str(), &m_open, flags)) {
        // Header
        if (m_summary.hasIssues()) {
            ImGui::TextColored(colors::kWarning, "Import Complete (with issues)");
        } else {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Import Complete");
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Stats section
        ImGui::Text("Total files: %d", m_summary.totalFiles);
        ImGui::Text("Successfully imported: %d", m_summary.successCount);

        if (m_summary.duplicateCount > 0) {
            ImGui::TextColored(colors::kWarning,
                               "Duplicates found: %d",
                               m_summary.duplicateCount);
        }

        if (m_summary.failedCount > 0) {
            ImGui::TextColored(colors::kErrorText, "Errors: %d", m_summary.failedCount);
        }

        ImGui::Spacing();

        // Interactive duplicates section
        if (m_summary.duplicateCount > 0 && !m_summary.duplicates.empty()) {
            if (ImGui::CollapsingHeader("Duplicates — Select to Re-import",
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                // Select All / Deselect All helpers
                if (ImGui::SmallButton("Select All")) {
                    std::fill(m_checked.begin(), m_checked.end(), true);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Deselect All")) {
                    std::fill(m_checked.begin(), m_checked.end(), false);
                }

                ImGui::Spacing();

                // Per-duplicate checkboxes
                for (size_t i = 0; i < m_summary.duplicates.size(); ++i) {
                    const auto& dup = m_summary.duplicates[i];
                    auto stem = file::getStem(dup.sourcePath);
                    std::string label = stem + "  ->  duplicate of: " + dup.existingName + "##dup" +
                                        std::to_string(i);
                    bool checked = m_checked[i];
                    if (ImGui::Checkbox(label.c_str(), &checked)) {
                        m_checked[i] = checked;
                    }
                }

                ImGui::Unindent();
            }

            ImGui::Spacing();

            // Count selected
            int selectedCount = 0;
            for (bool c : m_checked)
                if (c)
                    selectedCount++;

            // Re-import / Skip buttons
            std::string reimportLabel = "Re-import Selected (" + std::to_string(selectedCount) +
                                        ")";
            bool hasSelection = selectedCount > 0;

            if (!hasSelection)
                ImGui::BeginDisabled();
            float reimportBtnW = ImGui::CalcTextSize(reimportLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 4;
            if (ImGui::Button(reimportLabel.c_str(), ImVec2(reimportBtnW, 0))) {
                // Collect checked duplicates and invoke callback
                std::vector<DuplicateRecord> selected;
                for (size_t i = 0; i < m_summary.duplicates.size(); ++i) {
                    if (m_checked[i])
                        selected.push_back(m_summary.duplicates[i]);
                }
                if (m_onReimport && !selected.empty())
                    m_onReimport(std::move(selected));
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
            if (!hasSelection)
                ImGui::EndDisabled();

            ImGui::SameLine();
            float skipBtnW = ImGui::CalcTextSize("Skip All").x + ImGui::GetStyle().FramePadding.x * 4;
            if (ImGui::Button("Skip All", ImVec2(skipBtnW, 0))) {
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
        }

        // Errors section (collapsible)
        if (m_summary.failedCount > 0 && !m_summary.errors.empty()) {
            if (ImGui::CollapsingHeader("Errors", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                for (const auto& [filename, error] : m_summary.errors) {
                    ImGui::PushStyleColor(ImGuiCol_Text, colors::kErrorText);
                    ImGui::BulletText("%s", filename.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Indent();
                    ImGui::TextWrapped("%s", error.c_str());
                    ImGui::Unindent();
                    ImGui::Spacing();
                }
                ImGui::Unindent();
            }
        }

        // If no duplicates, show simple OK button
        if (m_summary.duplicateCount == 0) {
            ImGui::Separator();
            ImGui::Spacing();
            float okBtnW = ImGui::CalcTextSize("OK").x + ImGui::GetStyle().FramePadding.x * 4;
            if (ImGui::Button("OK", ImVec2(okBtnW, 0))) {
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

} // namespace dw
