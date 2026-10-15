#include "project_panel.h"

#include <string>
#include <vector>

#include <imgui.h>

#include "ui/icons.h"

namespace dw {
namespace {

const char* contentIcon(ProjectContentKind kind) {
    switch (kind) {
    case ProjectContentKind::Design: return Icons::Model;
    case ProjectContentKind::Setup: return Icons::Settings;
    case ProjectContentKind::Material: return Icons::Material;
    case ProjectContentKind::Tool: return Icons::List;
    case ProjectContentKind::GCode: return Icons::GCode;
    case ProjectContentKind::Run: return Icons::Play;
    }
    return Icons::File;
}

} // namespace

ProjectPanel::ProjectPanel() : Panel("Project") {}

void ProjectPanel::render() {
    if (!m_open) return;

    applyMinSize(14, 8);
    if (ImGui::Begin(m_title.c_str(), closeHandle())) {
        const auto snapshot = m_snapshotProvider ? m_snapshotProvider() : std::nullopt;
        if (snapshot) {
            renderProjectHeader(*snapshot);
            renderProjectContents(*snapshot);
            ImGui::Spacing();
            renderProjectDetails(*snapshot);
        } else {
            renderNoProject();
        }
    }
    ImGui::End();
}

void ProjectPanel::renderProjectContents(const ProjectPanelSnapshot& snapshot) {
    ImGui::SeparatorText("Contents");
    if (ImGui::Button("Add Design from Library", ImVec2(-1.0F, 0.0F)) &&
        m_addFromLibraryCallback) {
        m_addFromLibraryCallback();
    }
    if (snapshot.contents.empty()) {
        ImGui::TextDisabled("Information: no designs yet. Add one from the Library.");
        return;
    }
    for (std::size_t index = 0; index < snapshot.contents.size(); ++index) {
        if (!snapshot.contents[index].parent) renderContentRow(snapshot, index);
    }
}

void ProjectPanel::renderContentRow(const ProjectPanelSnapshot& snapshot, std::size_t index) {
    const auto& row = snapshot.contents[index];
    // Rows arrive depth-first, so a row's children always follow it.
    std::vector<std::size_t> children;
    for (std::size_t next = index + 1; next < snapshot.contents.size(); ++next) {
        if (snapshot.contents[next].parent == row.ref.item) children.push_back(next);
    }

    const std::string stableId = "project_item_" + std::to_string(row.ref.item.value);
    ImGui::PushID(stableId.c_str());

    // One line per item, like a slicer's object list: icon and name, with
    // the state in words at the right edge.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    // Designs open by default so their setups are visible; deeper G-code and
    // run history stay folded until asked for.
    if (row.kind == ProjectContentKind::Design) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool selected = snapshot.activeItem && *snapshot.activeItem == row.ref.item;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    const std::string label =
        std::string(contentIcon(row.kind)) + " " + row.label + "##project_item";
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    const bool activated =
        (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ||
        (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s: %s\n%s", projectContentKindLabel(row.kind),
                          row.label.c_str(), row.status.c_str());
    }
    renderContentRowMenu(row);

    const std::string status = row.needsAttention
                                   ? std::string(Icons::Warning) + " " + row.status
                                   : row.status;
    ImGui::SameLine();
    const float statusWidth = ImGui::CalcTextSize(status.c_str()).x;
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > statusWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - statusWidth);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(row.needsAttention ? ImGuiCol_Text
                                                                          : ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::NewLine();
    }

    // The selected design offers its next step, as a slicer selection does.
    if (selected && row.kind == ProjectContentKind::Design) {
        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
        if (ImGui::SmallButton("Open in CAM") && m_onOpenInCam) m_onOpenInCam(row.ref);
        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
    }
    if (activated && m_onProjectItemActivated)
        m_onProjectItemActivated(row.ref);

    if (!children.empty() && open) {
        for (const auto child : children) renderContentRow(snapshot, child);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ProjectPanel::renderContentRowMenu(const ProjectContentRow& row) {
    if (!ImGui::BeginPopupContextItem("##project_item_menu"))
        return;
    ImGui::TextDisabled("%s", projectContentKindLabel(row.kind));
    ImGui::Separator();
    const char* openLabel = row.kind == ProjectContentKind::Setup ? "Open in CAM" : "Open";
    if (ImGui::MenuItem(openLabel) && m_onProjectItemActivated)
        m_onProjectItemActivated(row.ref);
    if (row.kind == ProjectContentKind::Design && ImGui::MenuItem("Open in CAM") && m_onOpenInCam)
        m_onOpenInCam(row.ref);
    ImGui::EndPopup();
}

} // namespace dw
