#include "project_panel.h"

#include <cstring>

#include <imgui.h>

#include "modules/project_plan/project_plan_presentation.h"
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

    std::string designs;
    std::size_t designCount = 0;
    for (const auto& node : snapshot.plan.nodes) {
        if (node.item.kind != project_plan::ItemKind::Model) continue;
        if (!designs.empty()) designs += ", ";
        designs += node.item.label;
        ++designCount;
    }
    if (designCount == 1)
        ImGui::TextWrapped("Design: %s", designs.c_str());
    else if (designCount > 1)
        ImGui::TextWrapped("Legacy project designs: %s", designs.c_str());

    if (snapshot.activeItem) {
        for (const auto& node : snapshot.plan.nodes) {
            if (node.item.ref.item != *snapshot.activeItem) continue;
            const std::string selected =
                "Selected: " + node.item.label + " | " +
                project_plan::itemStateLabel(node.item.state);
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", selected.c_str());
            ImGui::PopStyleColor();
            break;
        }
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
