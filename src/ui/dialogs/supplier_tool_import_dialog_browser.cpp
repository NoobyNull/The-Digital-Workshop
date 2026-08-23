#include "supplier_tool_import_dialog.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace dw {
namespace {

bool isGeneratedFolder(SupplierToolImportFolderKind kind) {
    return kind == SupplierToolImportFolderKind::GeneratedType ||
           kind == SupplierToolImportFolderKind::GeneratedSize;
}

std::string folderDisplayLabel(const SupplierToolImportFolder& folder,
                               std::size_t shownCount,
                               bool filtering) {
    std::string label = folder.label;
    if (isGeneratedFolder(folder.kind))
        label += " *";
    label += "  (" + std::to_string(shownCount);
    if (filtering)
        label += "/" + std::to_string(folder.totalToolCount);
    label += ")";
    return label;
}

} // namespace

const SupplierToolImportFolder* SupplierToolImportDialog::activeFolder() const {
    const auto* folder = m_tree.folder(m_activeFolderId);
    return folder ? folder : m_tree.folder(m_tree.rootId());
}

std::vector<const SupplierToolImportRow*> SupplierToolImportDialog::scopedRows(
    const std::string& query) const {
    const auto* folder = activeFolder();
    if (!folder)
        return {};

    const auto matchingIds = m_tree.matchingToolIds(folder->id, m_rows, query);
    const std::set<std::string> wanted(matchingIds.begin(), matchingIds.end());
    std::vector<const SupplierToolImportRow*> result;
    result.reserve(matchingIds.size());
    for (const auto& row : m_rows) {
        if (wanted.count(row.geometryId) > 0)
            result.push_back(&row);
    }
    return result;
}

void SupplierToolImportDialog::renderFolderNode(const std::string& folderId,
                                                const std::string& query) {
    const auto* folder = m_tree.folder(folderId);
    if (!folder)
        return;

    auto children = m_tree.children(folderId);
    if (!query.empty()) {
        children.erase(
            std::remove_if(children.begin(), children.end(), [&](const auto* child) {
                return m_tree.matchingToolIds(child->id, m_rows, query).empty();
            }),
            children.end());
    }

    const auto shownIds = m_tree.matchingToolIds(folderId, m_rows, query);
    const auto eligibleIds = m_tree.matchingToolIds(folderId, m_rows, query, true);
    const auto state = m_tree.selectionState(folderId, m_rows, m_selection, query);
    bool branchSelected = state == SupplierToolImportSelectionState::Checked;

    ImGui::PushID(folderId.c_str());
    if (eligibleIds.empty())
        ImGui::BeginDisabled();
    if (state == SupplierToolImportSelectionState::Mixed)
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    if (ImGui::Checkbox("##branchSelection", &branchSelected)) {
        m_tree.setBranchSelected(folderId, m_rows, query, branchSelected, m_selection);
    }
    if (state == SupplierToolImportSelectionState::Mixed)
        ImGui::PopItemFlag();
    if (eligibleIds.empty())
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Select or clear the matching tools in this folder and its subfolders.");
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_activeFolderId == folderId)
        flags |= ImGuiTreeNodeFlags_Selected;
    const bool hasChildren = !children.empty();
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (!query.empty() && hasChildren)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    else if (folderId == m_tree.rootId() ||
             (folder->parentId == m_tree.rootId() &&
              folder->kind == SupplierToolImportFolderKind::Supplier)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    const std::size_t shownCount = query.empty() ? folder->totalToolCount : shownIds.size();
    const std::string label = folderDisplayLabel(*folder, shownCount, !query.empty());
    const bool open = ImGui::TreeNodeEx("##folder", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        m_activeFolderId = folderId;

    if (open && hasChildren) {
        for (const auto* child : children)
            renderFolderNode(child->id, query);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void SupplierToolImportDialog::renderFolderPane(const std::string& query,
                                                float width,
                                                float height) {
    if (ImGui::BeginChild("##supplierFolderPane", ImVec2(width, height),
                          ImGuiChildFlags_Borders)) {
        ImGui::TextUnformatted("Folders");
        ImGui::TextDisabled("Checkbox selects a branch; name browses it.");

        const bool hasGeneratedFolders = std::any_of(
            m_rows.begin(), m_rows.end(), [](const auto& row) {
                return row.categoryPath.empty();
            });
        if (hasGeneratedFolders)
            ImGui::TextDisabled("* Generated from tool type and size (supplier folders missing).");
        ImGui::Separator();

        renderFolderNode(m_tree.rootId(), query);
        if (!query.empty() &&
            m_tree.matchingToolIds(m_tree.rootId(), m_rows, query).empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No folders contain matching tools.");
        }
    }
    ImGui::EndChild();
}

void SupplierToolImportDialog::renderToolPane(const std::string& query,
                                              float width,
                                              float height) {
    if (!ImGui::BeginChild("##supplierToolPane", ImVec2(width, height),
                           ImGuiChildFlags_Borders,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild();
        return;
    }

    const auto* folder = activeFolder();
    const auto rows = scopedRows(query);
    const auto allInScope = folder ? m_tree.toolIds(folder->id) : std::vector<std::string>{};
    ImGui::TextUnformatted(folder ? folder->label.c_str() : "All Tools");
    if (folder && isGeneratedFolder(folder->kind))
        ImGui::TextDisabled("Generated grouping based on tool type or size.");
    else
        ImGui::TextDisabled("Includes tools in this folder and its subfolders.");
    ImGui::Separator();

    if (allInScope.empty()) {
        ImGui::TextDisabled("This folder does not contain any tools.");
        ImGui::EndChild();
        return;
    }
    if (rows.empty()) {
        ImGui::TextDisabled("No tools in this folder match the current search.");
        ImGui::EndChild();
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##supplierToolTable", 6, flags,
                          ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("Tool", ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Flutes", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 4.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableHeadersRow();

        for (const auto* row : rows) {
            ImGui::PushID(row->geometryId.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = m_selection.contains(row->geometryId);
            if (row->alreadyLocal)
                ImGui::BeginDisabled();
            if (ImGui::Checkbox("##selected", &selected))
                m_selection.setSelected(row->geometryId, selected);
            if (row->alreadyLocal)
                ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row->displayName.c_str());
            if (!row->folderPath.empty())
                ImGui::TextDisabled("%s", row->folderPath.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row->toolType.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(row->size.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", row->flutes);
            ImGui::TableSetColumnIndex(5);
            if (row->alreadyLocal)
                ImGui::TextUnformatted(toolStatusLabel(*row));
            else
                ImGui::TextDisabled("%s", toolStatusLabel(*row));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void SupplierToolImportDialog::renderBrowser(const std::string& query, float height) {
    if (!ImGui::BeginChild("##supplierBrowser", ImVec2(0.0f, height),
                           ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild();
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    constexpr float sideBySideThreshold = 720.0f;
    if (available.x >= sideBySideThreshold) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float folderWidth = std::clamp(available.x * 0.30f, 250.0f, 360.0f);
        renderFolderPane(query, folderWidth, available.y);
        ImGui::SameLine(0.0f, spacing);
        renderToolPane(query, 0.0f, available.y);
    } else {
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        const float folderHeight = std::max(145.0f, available.y * 0.38f);
        renderFolderPane(query, 0.0f, folderHeight);
        ImGui::Dummy(ImVec2(0.0f, spacing));
        renderToolPane(query, 0.0f, 0.0f);
    }
    ImGui::EndChild();
}

} // namespace dw
