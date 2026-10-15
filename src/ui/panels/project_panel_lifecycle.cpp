#include "project_panel.h"

#include <cstring>
#include <string>

#include <imgui.h>

#include "ui/icons.h"

namespace dw {

void ProjectPanel::renderProjectHeader(const ProjectPanelSnapshot& snapshot) {
    std::string label = std::string(Icons::Project) + " " + snapshot.projectName;
    if (snapshot.modified) label += " *";
    ImGui::TextWrapped("%s", label.c_str());
    if (!snapshot.description.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", snapshot.description.c_str());
        ImGui::PopStyleColor();
    }

    if (!snapshot.activeItem) return;
    for (const auto& row : snapshot.contents) {
        if (row.ref.item != *snapshot.activeItem) continue;
        const std::string selected = "Selected: " + row.label + " | " + row.status;
        ImGui::PushStyleColor(
            ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", selected.c_str());
        ImGui::PopStyleColor();
        break;
    }
}

void ProjectPanel::renderProjectDetails(const ProjectPanelSnapshot& snapshot) {
    if (!ImGui::CollapsingHeader("Project Details")) return;
    ImGui::Indent();

    if (m_notesProjectId != snapshot.projectId) {
        std::strncpy(m_notesBuf, snapshot.notes.c_str(), sizeof(m_notesBuf) - 1);
        m_notesBuf[sizeof(m_notesBuf) - 1] = '\0';
        m_notesProjectId = snapshot.projectId;
        m_notesChanged = false;
    }
    ImGui::TextDisabled("Notes");
    if (ImGui::InputTextMultiline("##project_notes", m_notesBuf,
                                  sizeof(m_notesBuf), ImVec2(-1.0f, 120.0f))) {
        m_notesChanged = true;
    }
    if (m_notesChanged && ImGui::IsItemDeactivatedAfterEdit()) {
        if (m_updateNotesCallback)
            m_updateNotesCallback(snapshot.projectId, m_notesBuf);
        m_notesChanged = false;
    }

    ImGui::Spacing();
    if (ImGui::Button("Save") && m_saveProjectCallback)
        (void)m_saveProjectCallback();
    ImGui::SameLine();
    if (ImGui::Button("Close") && m_closeProjectCallback)
        m_closeProjectCallback();

    if (!snapshot.hasModels) ImGui::BeginDisabled();
    if (ImGui::Button("Export .dwproj") && m_exportProjectCallback)
        m_exportProjectCallback();
    if (!snapshot.hasModels) ImGui::EndDisabled();
    if (!snapshot.hasModels &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Choose a model before exporting this project");
    }
    ImGui::Unindent();
}

void ProjectPanel::renderNoProject() {
    ImGui::TextDisabled("No project open");
    ImGui::Spacing();
    ImGui::TextWrapped("Home is the one place to start or continue a project.");
    ImGui::Spacing();
    if (ImGui::Button("Open Home") && m_openHomeCallback)
        m_openHomeCallback();
}

} // namespace dw
