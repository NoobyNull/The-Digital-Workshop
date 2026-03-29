#include "ui/panels/tool_browser_panel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/config/config.h"
#include "core/database/tool_database.h"
#include "core/database/toolbox_repository.h"
#include "core/utils/uuid.h"
#include "core/gcode/machine_profile.h"
#include "core/materials/material_manager.h"
#include "core/utils/log.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/icons.h"

namespace dw {

// Tool type display names
static const char* toolTypeName(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose: return "Ball Nose";
    case VtdbToolType::EndMill: return "End Mill";
    case VtdbToolType::Radiused: return "Radiused";
    case VtdbToolType::VBit: return "V-Bit";
    case VtdbToolType::TaperedBallNose: return "Tapered Ball Nose";
    case VtdbToolType::Drill: return "Drill";
    case VtdbToolType::ThreadMill: return "Thread Mill";
    case VtdbToolType::FormTool: return "Form Tool";
    case VtdbToolType::DiamondDrag: return "Diamond Drag";
    default: return "Unknown";
    }
}

// Auto-format a display name from tool geometry when tree entry name is empty.
// Prefers the resolved name_format (which may contain the manufacturer part number),
// falling back to a generic "Type Xmm N-flute" label.
static std::string autoFormatToolName(const VtdbToolGeometry& g) {
    std::string resolved = resolveToolNameFormat(g);
    if (!resolved.empty())
        return resolved;
    char buf[128];
    f64 diam = (g.units == VtdbUnits::Imperial) ? g.diameter * 25.4 : g.diameter;
    std::snprintf(buf, sizeof(buf), "%s %.3gmm %d-flute",
                  toolTypeName(g.tool_type), diam, g.num_flutes);
    return buf;
}

static constexpr int kToolTypeCount = 9;
static const VtdbToolType kToolTypes[] = {
    VtdbToolType::BallNose,
    VtdbToolType::EndMill,
    VtdbToolType::Radiused,
    VtdbToolType::VBit,
    VtdbToolType::TaperedBallNose,
    VtdbToolType::Drill,
    VtdbToolType::ThreadMill,
    VtdbToolType::FormTool,
    VtdbToolType::DiamondDrag,
};

ToolBrowserPanel::ToolBrowserPanel() : Panel("Tool Browser") {}

void ToolBrowserPanel::render() {
    if (!m_open) return;

    if (m_needsRefresh && m_toolDatabase) {
        loadData();
        m_needsRefresh = false;
        if (m_filterBuf[0] != '\0') updateFilter();
    }

    applyMinSize(22, 10);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    if (!m_toolDatabase) {
        ImGui::TextDisabled("No tool database loaded");
        ImGui::End();
        return;
    }

    renderToolbar();

    // Search / filter bar
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##toolFilter", "Search tools...",
                                  m_filterBuf, sizeof(m_filterBuf)))
        updateFilter();
    ImGui::Separator();

    // Two-column layout: tree (left) | detail (right)
    // Calculate minimum tree width from content so labels aren't truncated
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float maxLabelW = 0.0f;
    float indent = ImGui::GetTreeNodeToLabelSpacing();
    for (const auto& entry : m_treeEntries) {
        // Estimate nesting depth by counting ancestors
        int depth = 0;
        std::string pid = entry.parent_group_id;
        while (!pid.empty() && depth < 8) {
            ++depth;
            bool found = false;
            for (const auto& e : m_treeEntries) {
                if (e.id == pid) { pid = e.parent_group_id; found = true; break; }
            }
            if (!found) break;
        }

        std::string displayName = entry.name;
        if (displayName.empty() && !entry.tool_geometry_id.empty()) {
            for (const auto& g : m_geometries) {
                if (g.id == entry.tool_geometry_id) {
                    displayName = autoFormatToolName(g);
                    break;
                }
            }
        }
        if (!displayName.empty()) {
            float textW = ImGui::CalcTextSize(displayName.c_str()).x;
            float totalW = indent * static_cast<float>(depth + 1) + textW
                         + ImGui::GetFontSize() * 2.0f; // icon + padding
            maxLabelW = std::max(maxLabelW, totalW);
        }
    }
    // Clamp: at least 20% of panel, at most 60%, with scrollbar padding
    float scrollbarW = ImGui::GetStyle().ScrollbarSize;
    float minTreeW = avail.x * 0.2f;
    float maxTreeW = avail.x * 0.6f;
    float treeWidth = std::clamp(maxLabelW + scrollbarW, minTreeW, maxTreeW);

    if (ImGui::BeginChild("ToolTree", ImVec2(treeWidth, avail.y), ImGuiChildFlags_Borders)) {
        bool hasFilter = m_filterBuf[0] != '\0';
        if (hasFilter && m_filterMatchIds.empty()) {
            ImGui::TextDisabled("No tools matching \"%s\"", m_filterBuf);
        } else {
            renderTree();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild("ToolDetail", ImVec2(0, avail.y), ImGuiChildFlags_Borders)) {
        renderToolDetail();
    }
    ImGui::EndChild();

    renderAddToolPopup();
    renderAddGroupPopup();

    ImGui::End();
}

void ToolBrowserPanel::renderToolbar() {
    auto iconBtn = [](const char* icon, const char* label) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s %s", icon, label);
        return ImGui::Button(buf);
    };

    if (iconBtn(Icons::Add, "Tool")) {
        m_showAddTool = true;
        m_addToolName.clear();
        m_addToolType = static_cast<int>(VtdbToolType::EndMill);
        m_addToolDiameter = 0.25f;
        m_addToolFlutes = 2;
        m_addToolParentGroupId.clear();
    }
    ImGui::SameLine();
    if (iconBtn(Icons::Folder, "Group")) {
        m_showAddGroup = true;
        m_addGroupName.clear();
        m_addGroupParentId.clear();
    }
    ImGui::SameLine();
    if (iconBtn(Icons::Delete, "Delete")) {
        deleteSelected();
    }
    ImGui::SameLine();
    if (iconBtn(Icons::Import, "Import .vtdb")) {
        if (m_fileDialog) {
            std::vector<FileFilter> filters = {{".vtdb Tool Database", "*.vtdb"}};
            m_fileDialog->showOpen(
                "Import Tool Database", filters, [this](const std::string& path) {
                    if (!path.empty() && m_toolDatabase) {
                        int count = m_toolDatabase->importFromVtdb(Path(path));
                        if (count >= 0) {
                            log::infof("ToolBrowser", "Imported %d tools from %s", count, path.c_str());
                        } else {
                            log::errorf("ToolBrowser", "Failed to import from %s", path.c_str());
                        }
                        refresh();
                    }
                });
        }
    }
    ImGui::SameLine();
    // Toolbox toggle button
    if (!m_selectedGeometryId.empty() && m_toolboxRepo) {
        bool inToolbox = m_toolboxIds.count(m_selectedGeometryId) > 0;
        char starLabel[64];
        std::snprintf(starLabel, sizeof(starLabel), "%s %s",
                      Icons::Star, inToolbox ? "Remove from Toolbox" : "Add to Toolbox");
        if (inToolbox) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.6f, 0.15f, 1.0f));
        }
        if (ImGui::Button(starLabel)) {
            if (inToolbox) {
                m_toolboxRepo->removeTool(m_selectedGeometryId);
                m_toolboxIds.erase(m_selectedGeometryId);
            } else {
                m_toolboxRepo->addTool(m_selectedGeometryId);
                m_toolboxIds.insert(m_selectedGeometryId);
            }
        }
        if (inToolbox) ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }
    if (ImGui::Button(icons::ICON_REFRESH)) {
        refresh();
    }
}

// Context menu action requested by tree rendering
struct TreeContextAction {
    std::string entryId;
    std::string geomId;
    enum Action { None, Rename, Duplicate, Toolbox, Delete } action = None;
};

// Recursive tree rendering helper
static void renderTreeNode(const std::vector<VtdbTreeEntry>& entries,
                           const std::vector<VtdbToolGeometry>& geometries,
                           const std::set<std::string>& toolboxIds,
                           const std::set<std::string>& filterIds,
                           bool filtering,
                           ToolboxRepository* toolboxRepo,
                           const std::string& parentId,
                           std::string& selectedId,
                           std::string& selectedGeomId,
                           TreeContextAction& ctxAction) {
    for (const auto& entry : entries) {
        if (entry.parent_group_id != parentId) continue;

        // Skip entries that don't match the filter
        if (filtering && filterIds.count(entry.id) == 0) continue;

        bool isGroup = entry.tool_geometry_id.empty();
        bool isSelected = (entry.id == selectedId);

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!isGroup)
            flags |= ImGuiTreeNodeFlags_Leaf
                   | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        // Auto-open groups when filtering
        if (filtering && isGroup)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        // Resolve display name
        std::string displayName = entry.name;
        if (displayName.empty() && !entry.tool_geometry_id.empty()) {
            for (const auto& g : geometries) {
                if (g.id == entry.tool_geometry_id) {
                    displayName = autoFormatToolName(g);
                    break;
                }
            }
            if (displayName.empty()) displayName = "(unnamed tool)";
        }

        const char* icon = isGroup ? Icons::Folder : Icons::Settings;
        bool inToolbox = !isGroup
            && toolboxIds.count(entry.tool_geometry_id) > 0;
        std::string prefix = std::string(icon);
        if (inToolbox) prefix += " " + std::string(Icons::Star);
        std::string label = prefix + " " + displayName
                          + "###" + entry.id;

        if (isGroup) {
            bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked()
                && !ImGui::IsItemToggledOpen()) {
                selectedId = entry.id;
                selectedGeomId.clear();
            }
            // Context menu for groups
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename"))
                    ctxAction = {entry.id, "",
                                 TreeContextAction::Rename};
                if (ImGui::MenuItem("Delete"))
                    ctxAction = {entry.id, "",
                                 TreeContextAction::Delete};
                ImGui::EndPopup();
            }
            if (nodeOpen) {
                renderTreeNode(entries, geometries, toolboxIds,
                               filterIds, filtering,
                               toolboxRepo, entry.id,
                               selectedId, selectedGeomId,
                               ctxAction);
                ImGui::TreePop();
            }
        } else {
            ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                selectedId = entry.id;
                selectedGeomId = entry.tool_geometry_id;
            }
            // Context menu for tools
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename"))
                    ctxAction = {entry.id,
                                 entry.tool_geometry_id,
                                 TreeContextAction::Rename};
                if (ImGui::MenuItem("Duplicate"))
                    ctxAction = {entry.id,
                                 entry.tool_geometry_id,
                                 TreeContextAction::Duplicate};
                if (toolboxRepo) {
                    const char* tbLabel = inToolbox
                        ? "Remove from My Toolbox"
                        : "Add to My Toolbox";
                    if (ImGui::MenuItem(tbLabel))
                        ctxAction = {entry.id,
                                     entry.tool_geometry_id,
                                     TreeContextAction::Toolbox};
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete"))
                    ctxAction = {entry.id,
                                 entry.tool_geometry_id,
                                 TreeContextAction::Delete};
                ImGui::EndPopup();
            }
        }
    }
}

// Case-insensitive substring match
static bool containsCI(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a))
                == std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

void ToolBrowserPanel::updateFilter() {
    m_filterMatchIds.clear();
    std::string query(m_filterBuf);
    if (query.empty()) return;

    // Match tools against name, type, diameter, notes
    for (const auto& entry : m_treeEntries) {
        if (entry.tool_geometry_id.empty()) continue; // skip groups

        // Build searchable text from entry name + geometry fields
        std::string text = entry.name;
        for (const auto& g : m_geometries) {
            if (g.id != entry.tool_geometry_id) continue;
            if (text.empty()) text = autoFormatToolName(g);
            text += " " + std::string(toolTypeName(g.tool_type));
            char diaBuf[32];
            std::snprintf(diaBuf, sizeof(diaBuf), " %.4g", g.diameter);
            text += diaBuf;
            if (!g.notes.empty()) text += " " + g.notes;
            if (!g.name_format.empty()) text += " " + g.name_format;
            break;
        }

        if (containsCI(text, query)) {
            // Add this entry and all its ancestors (so the tree path is visible)
            m_filterMatchIds.insert(entry.id);
            std::string pid = entry.parent_group_id;
            while (!pid.empty()) {
                m_filterMatchIds.insert(pid);
                bool found = false;
                for (const auto& e : m_treeEntries) {
                    if (e.id == pid) {
                        pid = e.parent_group_id;
                        found = true;
                        break;
                    }
                }
                if (!found) break;
            }
        }
    }
}

void ToolBrowserPanel::renderTree() {
    TreeContextAction ctxAction;
    bool filtering = m_filterBuf[0] != '\0'
                     && !m_filterMatchIds.empty();
    renderTreeNode(m_treeEntries, m_geometries, m_toolboxIds,
                   m_filterMatchIds, filtering,
                   m_toolboxRepo, "",
                   m_selectedTreeEntryId, m_selectedGeometryId,
                   ctxAction);

    // Handle context menu actions
    switch (ctxAction.action) {
    case TreeContextAction::Rename:
        m_contextMenuEntryId = ctxAction.entryId;
        m_contextMenuGeomId = ctxAction.geomId;
        m_showRenamePopup = true;
        // Pre-fill rename buffer with current name
        m_renameBuf[0] = '\0';
        for (const auto& e : m_treeEntries) {
            if (e.id == ctxAction.entryId) {
                std::string name = e.name;
                if (name.empty() && !e.tool_geometry_id.empty()) {
                    for (const auto& g : m_geometries) {
                        if (g.id == e.tool_geometry_id) {
                            name = autoFormatToolName(g);
                            break;
                        }
                    }
                }
                std::strncpy(m_renameBuf, name.c_str(),
                             sizeof(m_renameBuf) - 1);
                break;
            }
        }
        ImGui::OpenPopup("Rename##ctx");
        break;

    case TreeContextAction::Duplicate:
        duplicateTool(ctxAction.entryId);
        break;

    case TreeContextAction::Toolbox:
        if (m_toolboxRepo && !ctxAction.geomId.empty()) {
            if (m_toolboxIds.count(ctxAction.geomId) > 0) {
                m_toolboxRepo->removeTool(ctxAction.geomId);
                m_toolboxIds.erase(ctxAction.geomId);
            } else {
                m_toolboxRepo->addTool(ctxAction.geomId);
                m_toolboxIds.insert(ctxAction.geomId);
            }
        }
        break;

    case TreeContextAction::Delete:
        m_contextMenuEntryId = ctxAction.entryId;
        m_contextMenuGeomId = ctxAction.geomId;
        m_showDeleteConfirm = true;
        ImGui::OpenPopup("Delete Tool?##ctx");
        break;

    case TreeContextAction::None:
        break;
    }

    // --- Rename popup ---
    if (ImGui::BeginPopup("Rename##ctx")) {
        ImGui::Text("Rename:");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
        bool enter = ImGui::InputText("##renameBuf", m_renameBuf,
                         sizeof(m_renameBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue);
        if (m_showRenamePopup) {
            ImGui::SetKeyboardFocusHere(-1);
            m_showRenamePopup = false;
        }
        ImGui::SameLine();
        if (enter || ImGui::Button("OK")) {
            // Apply rename to tree entry
            for (auto& e : m_treeEntries) {
                if (e.id == m_contextMenuEntryId) {
                    e.name = m_renameBuf;
                    if (m_toolDatabase)
                        m_toolDatabase->updateTreeEntry(e);
                    break;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Delete confirmation popup ---
    if (ImGui::BeginPopup("Delete Tool?##ctx")) {
        ImGui::Text("Delete this item?");
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Spacing();
        {
            const float pad = ImGui::GetStyle().FramePadding.x * 4.0f;
            const float deleteW = ImGui::CalcTextSize("Delete").x + pad;
            const float cancelW = ImGui::CalcTextSize("Cancel").x + pad;
            if (ImGui::Button("Delete", ImVec2(deleteW, 0))) {
                // Temporarily set selection to perform delete
                std::string prevSelId = m_selectedTreeEntryId;
                m_selectedTreeEntryId = m_contextMenuEntryId;
                deleteSelected();
                if (m_selectedTreeEntryId.empty()
                    && prevSelId != m_contextMenuEntryId) {
                    m_selectedTreeEntryId = prevSelId;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(cancelW, 0)))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ToolBrowserPanel::renderToolDetail() {
    if (m_selectedGeometryId.empty()) {
        ImGui::TextDisabled("Select a tool to view details");
        return;
    }

    // Toolbox toggle
    if (m_toolboxRepo) {
        bool inToolbox = m_toolboxIds.count(m_selectedGeometryId) > 0;
        if (ImGui::Checkbox("In My Toolbox", &inToolbox)) {
            if (inToolbox) {
                m_toolboxRepo->addTool(m_selectedGeometryId);
                m_toolboxIds.insert(m_selectedGeometryId);
            } else {
                m_toolboxRepo->removeTool(m_selectedGeometryId);
                m_toolboxIds.erase(m_selectedGeometryId);
            }
        }
        ImGui::Spacing();
    }

    // Geometry section
    ImGui::SeparatorText("Geometry");

    // Tool type dropdown
    const char* currentTypeName = toolTypeName(m_editGeometry.tool_type);
    if (ImGui::BeginCombo("Tool Type", currentTypeName)) {
        for (int i = 0; i < kToolTypeCount; ++i) {
            bool selected = (m_editGeometry.tool_type == kToolTypes[i]);
            if (ImGui::Selectable(toolTypeName(kToolTypes[i]), selected)) {
                m_editGeometry.tool_type = kToolTypes[i];
            }
        }
        ImGui::EndCombo();
    }

    // Units
    const char* unitsLabels[] = {"Metric (mm)", "Imperial (in)"};
    int unitsIdx = static_cast<int>(m_editGeometry.units);
    if (ImGui::Combo("Units", &unitsIdx, unitsLabels, 2)) {
        m_editGeometry.units = static_cast<VtdbUnits>(unitsIdx);
    }

    const char* unitSuffix = (m_editGeometry.units == VtdbUnits::Metric) ? "mm" : "in";

    // Core dimensions
    float diameter = static_cast<float>(m_editGeometry.diameter);
    if (ImGui::InputFloat("Diameter", &diameter, 0.01f, 0.1f, "%.4f")) {
        m_editGeometry.diameter = diameter;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", unitSuffix);

    int flutes = m_editGeometry.num_flutes;
    if (ImGui::InputInt("Flutes", &flutes)) {
        m_editGeometry.num_flutes = std::max(1, flutes);
    }

    float fluteLen = static_cast<float>(m_editGeometry.flute_length);
    if (ImGui::InputFloat("Flute Length", &fluteLen, 0.01f, 0.1f, "%.4f")) {
        m_editGeometry.flute_length = fluteLen;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", unitSuffix);

    // V-bit specific fields
    if (m_editGeometry.tool_type == VtdbToolType::VBit) {
        float angle = static_cast<float>(m_editGeometry.included_angle);
        if (ImGui::InputFloat("Included Angle", &angle, 1.0f, 5.0f, "%.1f")) {
            m_editGeometry.included_angle = angle;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("deg");

        float flatDia = static_cast<float>(m_editGeometry.flat_diameter);
        if (ImGui::InputFloat("Flat Diameter", &flatDia, 0.001f, 0.01f, "%.4f")) {
            m_editGeometry.flat_diameter = flatDia;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", unitSuffix);
    }

    // Notes
    char notesBuf[512] = {};
    std::strncpy(notesBuf, m_editGeometry.notes.c_str(), sizeof(notesBuf) - 1);
    if (ImGui::InputTextMultiline("Notes##geom", notesBuf, sizeof(notesBuf), ImVec2(-1, 60))) {
        m_editGeometry.notes = notesBuf;
    }

    // Cutting Data section
    ImGui::Spacing();
    ImGui::SeparatorText("Cutting Data");

    // Material selector
    if (ImGui::BeginCombo("Material##cd", m_selectedMaterialId.empty()
                                              ? "(All Materials)"
                                              : m_selectedMaterialId.c_str())) {
        // Find the name for display
        if (ImGui::Selectable("(All Materials)", m_selectedMaterialId.empty())) {
            m_selectedMaterialId.clear();
            selectTool(m_selectedGeometryId); // reload cutting data
        }
        for (const auto& mat : m_materials) {
            bool sel = (mat.id == m_selectedMaterialId);
            if (ImGui::Selectable(mat.name.c_str(), sel)) {
                m_selectedMaterialId = mat.id;
                selectTool(m_selectedGeometryId);
            }
        }
        ImGui::EndCombo();
    }

    // Machine selector
    if (ImGui::BeginCombo("Machine##cd", m_selectedMachineId.empty()
                                             ? "(Default Machine)"
                                             : m_selectedMachineId.c_str())) {
        if (ImGui::Selectable("(Default Machine)", m_selectedMachineId.empty())) {
            m_selectedMachineId.clear();
            selectTool(m_selectedGeometryId);
        }
        for (const auto& mach : m_machines) {
            bool sel = (mach.id == m_selectedMachineId);
            std::string machLabel = mach.name;
            if (!mach.make.empty()) machLabel += " (" + mach.make + ")";
            if (ImGui::Selectable(machLabel.c_str(), sel)) {
                m_selectedMachineId = mach.id;
                selectTool(m_selectedGeometryId);
            }
        }
        ImGui::EndCombo();
    }

    if (m_hasCuttingData) {
        float feedRate = static_cast<float>(m_editCuttingData.feed_rate);
        if (ImGui::InputFloat("Feed Rate", &feedRate, 1.0f, 10.0f, "%.1f")) {
            m_editCuttingData.feed_rate = feedRate;
        }

        float plungeRate = static_cast<float>(m_editCuttingData.plunge_rate);
        if (ImGui::InputFloat("Plunge Rate", &plungeRate, 1.0f, 10.0f, "%.1f")) {
            m_editCuttingData.plunge_rate = plungeRate;
        }

        int spindleSpeed = m_editCuttingData.spindle_speed;
        if (ImGui::InputInt("Spindle Speed (RPM)", &spindleSpeed, 100, 1000)) {
            m_editCuttingData.spindle_speed = std::max(0, spindleSpeed);
        }

        float stepdown = static_cast<float>(m_editCuttingData.stepdown);
        if (ImGui::InputFloat("Stepdown", &stepdown, 0.01f, 0.1f, "%.4f")) {
            m_editCuttingData.stepdown = stepdown;
        }

        float stepover = static_cast<float>(m_editCuttingData.stepover);
        if (ImGui::InputFloat("Stepover", &stepover, 0.01f, 0.1f, "%.4f")) {
            m_editCuttingData.stepover = stepover;
        }

        int toolNum = m_editCuttingData.tool_number;
        if (ImGui::InputInt("Tool Number", &toolNum)) {
            m_editCuttingData.tool_number = std::max(0, toolNum);
        }
    } else {
        ImGui::TextDisabled("No cutting data for this material/machine combination");
        if (ImGui::Button("Add Cutting Data")) {
            VtdbCuttingData cd;
            cd.feed_rate = 100.0;
            cd.plunge_rate = 50.0;
            cd.spindle_speed = 18000;
            cd.stepdown = 0.1;
            cd.stepover = 0.4;
            m_toolDatabase->insertCuttingData(cd);

            // Create entity linking geometry + material + machine -> cutting data
            VtdbToolEntity entity;
            entity.tool_geometry_id = m_selectedGeometryId;
            entity.material_id = m_selectedMaterialId;
            entity.machine_id = m_selectedMachineId;
            entity.tool_cutting_data_id = cd.id;
            m_toolDatabase->insertEntity(entity);

            selectTool(m_selectedGeometryId);
        }
    }

    // Save button
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    char saveBtnLabel[64];
    std::snprintf(saveBtnLabel, sizeof(saveBtnLabel), "%s Save Changes", Icons::Save);
    float saveBtnW = ImGui::CalcTextSize(saveBtnLabel).x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button(saveBtnLabel, ImVec2(saveBtnW, 0))) {
        m_toolDatabase->updateGeometry(m_editGeometry);
        if (m_hasCuttingData) {
            m_toolDatabase->updateCuttingData(m_editCuttingData);
        }
        log::info("ToolBrowser", "Tool changes saved");
    }

    // Calculator section
    renderCalculator();

    // Machine editor section
    renderMachineEditor();
}

void ToolBrowserPanel::renderAddToolPopup() {
    if (m_showAddTool) {
        ImGui::OpenPopup("Add Tool");
        m_showAddTool = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Add Tool", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Tool type
        if (ImGui::BeginCombo("Type", toolTypeName(static_cast<VtdbToolType>(m_addToolType)))) {
            for (int i = 0; i < kToolTypeCount; ++i) {
                if (ImGui::Selectable(toolTypeName(kToolTypes[i]),
                                      m_addToolType == static_cast<int>(kToolTypes[i]))) {
                    m_addToolType = static_cast<int>(kToolTypes[i]);
                }
            }
            ImGui::EndCombo();
        }

        char nameBuf[256] = {};
        std::strncpy(nameBuf, m_addToolName.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            m_addToolName = nameBuf;
        }

        ImGui::InputFloat("Diameter", &m_addToolDiameter, 0.01f, 0.1f, "%.4f");
        ImGui::InputInt("Flutes", &m_addToolFlutes);
        m_addToolFlutes = std::max(1, m_addToolFlutes);

        // Parent group selector
        if (ImGui::BeginCombo("Parent Group", m_addToolParentGroupId.empty()
                                                  ? "(Root)"
                                                  : m_addToolParentGroupId.c_str())) {
            if (ImGui::Selectable("(Root)", m_addToolParentGroupId.empty())) {
                m_addToolParentGroupId.clear();
            }
            for (const auto& entry : m_treeEntries) {
                if (entry.tool_geometry_id.empty()) { // groups only
                    if (ImGui::Selectable(entry.name.c_str(),
                                          entry.id == m_addToolParentGroupId)) {
                        m_addToolParentGroupId = entry.id;
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        float toolDlgBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        if (ImGui::Button("Create", ImVec2(toolDlgBtnW, 0))) {
            // Auto-generate name if empty
            if (m_addToolName.empty()) {
                char autoName[128];
                std::snprintf(autoName, sizeof(autoName), "%s %.4f\" %dF",
                              toolTypeName(static_cast<VtdbToolType>(m_addToolType)),
                              m_addToolDiameter, m_addToolFlutes);
                m_addToolName = autoName;
            }

            VtdbToolGeometry geom;
            geom.tool_type = static_cast<VtdbToolType>(m_addToolType);
            geom.diameter = m_addToolDiameter;
            geom.num_flutes = m_addToolFlutes;
            m_toolDatabase->insertGeometry(geom);

            VtdbTreeEntry te;
            te.parent_group_id = m_addToolParentGroupId;
            te.tool_geometry_id = geom.id;
            te.name = m_addToolName;
            m_toolDatabase->insertTreeEntry(te);

            refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(toolDlgBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ToolBrowserPanel::renderAddGroupPopup() {
    if (m_showAddGroup) {
        ImGui::OpenPopup("Add Group");
        m_showAddGroup = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Add Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char nameBuf[256] = {};
        std::strncpy(nameBuf, m_addGroupName.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Group Name", nameBuf, sizeof(nameBuf))) {
            m_addGroupName = nameBuf;
        }

        // Parent group selector
        if (ImGui::BeginCombo("Parent", m_addGroupParentId.empty()
                                            ? "(Root)"
                                            : m_addGroupParentId.c_str())) {
            if (ImGui::Selectable("(Root)", m_addGroupParentId.empty())) {
                m_addGroupParentId.clear();
            }
            for (const auto& entry : m_treeEntries) {
                if (entry.tool_geometry_id.empty()) {
                    if (ImGui::Selectable(entry.name.c_str(),
                                          entry.id == m_addGroupParentId)) {
                        m_addGroupParentId = entry.id;
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        float grpBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        if (ImGui::Button("Create", ImVec2(grpBtnW, 0))) {
            if (!m_addGroupName.empty()) {
                VtdbTreeEntry te;
                te.parent_group_id = m_addGroupParentId;
                te.name = m_addGroupName;
                // tool_geometry_id left empty = group node
                m_toolDatabase->insertTreeEntry(te);
                refresh();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(grpBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ToolBrowserPanel::refresh() {
    m_needsRefresh = true;
}

void ToolBrowserPanel::loadData() {
    if (!m_toolDatabase) return;

    m_treeEntries = m_toolDatabase->getAllTreeEntries();
    m_materials = m_toolDatabase->findAllMaterials();
    m_machines = m_toolDatabase->findAllMachines();
    m_geometries = m_toolDatabase->findAllGeometries();

    // Load toolbox state
    if (m_toolboxRepo) {
        auto ids = m_toolboxRepo->getAllGeometryIds();
        m_toolboxIds = std::set<std::string>(ids.begin(), ids.end());
    }

    // Sort tree entries by sibling_order
    std::sort(m_treeEntries.begin(), m_treeEntries.end(),
              [](const VtdbTreeEntry& a, const VtdbTreeEntry& b) {
                  return a.sibling_order < b.sibling_order;
              });

    // Reload selected tool if still valid
    if (!m_selectedGeometryId.empty()) {
        selectTool(m_selectedGeometryId);
    }
}

void ToolBrowserPanel::selectTool(const std::string& geometryId) {
    m_selectedGeometryId = geometryId;

    auto geom = m_toolDatabase->findGeometryById(geometryId);
    if (geom) {
        m_editGeometry = *geom;
    }

    // Try to load cutting data for this geometry + material + machine
    auto view = m_toolDatabase->getToolView(geometryId, m_selectedMaterialId, m_selectedMachineId);
    if (view) {
        m_editCuttingData = view->cutting_data;
        m_hasCuttingData = true;
    } else {
        m_editCuttingData = {};
        m_hasCuttingData = false;
    }
}

static const char* driveTypeName(DriveType dt) {
    switch (dt) {
    case DriveType::Belt: return "Belt";
    case DriveType::LeadScrew: return "Lead Screw";
    case DriveType::BallScrew: return "Ball Screw";
    case DriveType::RackPinion: return "Rack & Pinion";
    }
    return "Unknown";
}

static const char* hardnessBandName(HardnessBand band) {
    switch (band) {
    case HardnessBand::Soft: return "Soft Wood";
    case HardnessBand::Medium: return "Medium Wood";
    case HardnessBand::Hard: return "Hard Wood";
    case HardnessBand::VeryHard: return "Very Hard Wood";
    case HardnessBand::Composite: return "Composite";
    case HardnessBand::Metal: return "Metal";
    case HardnessBand::Plastic: return "Plastic";
    }
    return "Unknown";
}

void ToolBrowserPanel::renderCalculator() {
    if (m_selectedGeometryId.empty()) return;

    ImGui::Spacing();
    ImGui::SeparatorText("Feeds & Speeds Calculator");

    // Material source: pick from MaterialManager or enter Janka manually
    if (m_materialManager) {
        auto allMats = m_materialManager->getAllMaterials();
        if (ImGui::BeginCombo("Workpiece Material", m_calcMaterialName.empty()
                                                        ? "(Select material)"
                                                        : m_calcMaterialName.c_str())) {
            for (const auto& mat : allMats) {
                if (ImGui::Selectable(mat.name.c_str(), mat.name == m_calcMaterialName)) {
                    m_calcMaterialName = mat.name;
                    m_calcJanka = mat.jankaHardness;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::InputFloat("Janka Hardness (lbf)", &m_calcJanka, 10.0f, 100.0f, "%.0f");

    // Machine display — .vtdb machine if selected, otherwise active Config profile
    std::string machLabel;
    VtdbMachine selectedMach;
    bool hasMachine = false;
    if (!m_selectedMachineId.empty()) {
        for (const auto& mach : m_machines) {
            if (mach.id == m_selectedMachineId) {
                selectedMach = mach;
                machLabel = mach.name;
                hasMachine = true;
                break;
            }
        }
    }
    // Fall back to active Config machine profile
    if (!hasMachine) {
        const auto& mp = Config::instance().getActiveMachineProfile();
        machLabel = mp.name;
        selectedMach.name = mp.name;
        selectedMach.spindle_power_watts = static_cast<f64>(mp.spindlePower);
        selectedMach.max_rpm = static_cast<int>(mp.spindleMaxRPM);
        switch (mp.driveSystem) {
        case gcode::DriveSystem::Belt:      selectedMach.drive_type = DriveType::Belt; break;
        case gcode::DriveSystem::BallScrew: selectedMach.drive_type = DriveType::BallScrew; break;
        default:                            selectedMach.drive_type = DriveType::LeadScrew; break;
        }
        hasMachine = true;
    }
    ImGui::Text("Machine: %s", machLabel.c_str());
    if (hasMachine) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s, %d RPM, %.0fW)",
                            driveTypeName(selectedMach.drive_type),
                            selectedMach.max_rpm,
                            selectedMach.spindle_power_watts);
    }

    // Calculate button
    char calcBtnLabel[64];
    std::snprintf(calcBtnLabel, sizeof(calcBtnLabel), "%s Calculate", Icons::Settings);
    float calcBtnW = ImGui::CalcTextSize(calcBtnLabel).x + ImGui::GetStyle().FramePadding.x * 4;
    if (ImGui::Button(calcBtnLabel, ImVec2(calcBtnW, 0))) {
        runCalculation();
    }

    // Show results
    if (m_hasCalcResult) {
        ImGui::Spacing();
        ImGui::Indent();

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Classification: %s",
                           hardnessBandName(m_calcResult.hardness_band));
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Rigidity Factor: %.0f%%",
                           m_calcResult.rigidity_factor * 100.0);

        ImGui::Spacing();

        const char* unit = (m_editGeometry.units == VtdbUnits::Metric) ? "mm" : "in";
        const char* feedUnit = (m_editGeometry.units == VtdbUnits::Metric) ? "mm/min" : "in/min";

        ImGui::Text("RPM:         %d", m_calcResult.rpm);
        ImGui::Text("Feed Rate:   %.1f %s", m_calcResult.feed_rate, feedUnit);
        ImGui::Text("Plunge Rate: %.1f %s", m_calcResult.plunge_rate, feedUnit);
        ImGui::Text("Stepdown:    %.4f %s", m_calcResult.stepdown, unit);
        ImGui::Text("Stepover:    %.4f %s", m_calcResult.stepover, unit);
        ImGui::Text("Chip Load:   %.4f %s/tooth", m_calcResult.chip_load, unit);

        if (m_calcResult.power_required > 0.0) {
            ImGui::Text("Power:       %.0f W", m_calcResult.power_required);
            if (m_calcResult.power_limited) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "(power limited)");
            }
        }

        ImGui::Unindent();
        ImGui::Spacing();

        // Apply button — writes calculated values into the cutting data editor
        char applyLabel[64];
        std::snprintf(applyLabel, sizeof(applyLabel), "%s Apply to Cutting Data", Icons::Save);
        if (ImGui::Button(applyLabel)) {
            m_editCuttingData.spindle_speed = m_calcResult.rpm;
            m_editCuttingData.feed_rate = m_calcResult.feed_rate;
            m_editCuttingData.plunge_rate = m_calcResult.plunge_rate;
            m_editCuttingData.stepdown = m_calcResult.stepdown;
            m_editCuttingData.stepover = m_calcResult.stepover;

            // If no cutting data existed, create one
            if (!m_hasCuttingData) {
                VtdbCuttingData cd;
                cd.spindle_speed = m_calcResult.rpm;
                cd.feed_rate = m_calcResult.feed_rate;
                cd.plunge_rate = m_calcResult.plunge_rate;
                cd.stepdown = m_calcResult.stepdown;
                cd.stepover = m_calcResult.stepover;
                m_toolDatabase->insertCuttingData(cd);

                VtdbToolEntity entity;
                entity.tool_geometry_id = m_selectedGeometryId;
                entity.material_id = m_selectedMaterialId;
                entity.machine_id = m_selectedMachineId;
                entity.tool_cutting_data_id = cd.id;
                m_toolDatabase->insertEntity(entity);

                selectTool(m_selectedGeometryId);
            }
        }
    }
}

void ToolBrowserPanel::renderMachineEditor() {
    ImGui::Spacing();
    ImGui::SeparatorText("Machine Setup");

    if (m_selectedMachineId.empty()) {
        ImGui::TextDisabled("Select a machine above to configure");
        return;
    }

    // Find the selected machine
    VtdbMachine* mach = nullptr;
    for (auto& m : m_machines) {
        if (m.id == m_selectedMachineId) {
            mach = &m;
            break;
        }
    }
    if (!mach) return;

    bool changed = false;

    float power = static_cast<float>(mach->spindle_power_watts);
    if (ImGui::InputFloat("Spindle Power (W)", &power, 10.0f, 100.0f, "%.0f")) {
        mach->spindle_power_watts = power;
        changed = true;
    }

    int rpm = mach->max_rpm;
    if (ImGui::InputInt("Max RPM", &rpm, 500, 1000)) {
        mach->max_rpm = std::max(1000, rpm);
        changed = true;
    }

    int driveIdx = static_cast<int>(mach->drive_type);
    const char* driveLabels[] = {"Belt", "Lead Screw", "Ball Screw", "Rack & Pinion"};
    if (ImGui::Combo("Drive Type", &driveIdx, driveLabels, 4)) {
        mach->drive_type = static_cast<DriveType>(driveIdx);
        changed = true;
    }

    ImGui::TextDisabled("Rigidity: %.0f%%", ToolCalculator::rigidityFactor(mach->drive_type) * 100.0);

    if (changed) {
        m_toolDatabase->updateMachine(*mach);
    }
}

void ToolBrowserPanel::runCalculation() {
    CalcInput input;
    input.diameter = m_editGeometry.diameter;
    input.num_flutes = m_editGeometry.num_flutes;
    input.tool_type = m_editGeometry.tool_type;
    input.units = m_editGeometry.units;
    input.janka_hardness = m_calcJanka;
    input.material_name = m_calcMaterialName;

    // Get machine params from .vtdb machine if selected
    bool foundMachine = false;
    for (const auto& mach : m_machines) {
        if (mach.id == m_selectedMachineId) {
            input.spindle_power_watts = mach.spindle_power_watts;
            input.max_rpm = mach.max_rpm;
            input.drive_type = mach.drive_type;
            foundMachine = true;
            break;
        }
    }

    // Fall back to active Config machine profile
    if (!foundMachine) {
        const auto& mp = Config::instance().getActiveMachineProfile();
        input.spindle_power_watts = static_cast<f64>(mp.spindlePower);
        input.max_rpm = static_cast<int>(mp.spindleMaxRPM);
        switch (mp.driveSystem) {
        case gcode::DriveSystem::Belt:      input.drive_type = DriveType::Belt; break;
        case gcode::DriveSystem::BallScrew: input.drive_type = DriveType::BallScrew; break;
        default:                            input.drive_type = DriveType::LeadScrew; break;
        }
    }

    m_calcResult = ToolCalculator::calculate(input);
    m_hasCalcResult = true;
}

void ToolBrowserPanel::duplicateTool(const std::string& treeEntryId) {
    if (!m_toolDatabase) return;

    // Find source tree entry
    const VtdbTreeEntry* srcEntry = nullptr;
    for (const auto& e : m_treeEntries) {
        if (e.id == treeEntryId) { srcEntry = &e; break; }
    }
    if (!srcEntry || srcEntry->tool_geometry_id.empty()) return;

    // Find source geometry
    const VtdbToolGeometry* srcGeom = nullptr;
    for (const auto& g : m_geometries) {
        if (g.id == srcEntry->tool_geometry_id) { srcGeom = &g; break; }
    }
    if (!srcGeom) return;

    // Duplicate geometry with new ID
    VtdbToolGeometry newGeom = *srcGeom;
    newGeom.id = uuid::generate();
    m_toolDatabase->insertGeometry(newGeom);

    // Duplicate all entities + cutting data
    auto entities = m_toolDatabase->findEntitiesForGeometry(srcGeom->id);
    for (const auto& ent : entities) {
        auto cd = m_toolDatabase->findCuttingDataById(
            ent.tool_cutting_data_id);
        VtdbCuttingData newCd = cd ? *cd : VtdbCuttingData{};
        newCd.id = uuid::generate();
        m_toolDatabase->insertCuttingData(newCd);

        VtdbToolEntity newEnt = ent;
        newEnt.id = uuid::generate();
        newEnt.tool_geometry_id = newGeom.id;
        newEnt.tool_cutting_data_id = newCd.id;
        m_toolDatabase->insertEntity(newEnt);
    }

    // Duplicate tree entry (same parent, append " (Copy)")
    VtdbTreeEntry newEntry = *srcEntry;
    newEntry.id = uuid::generate();
    newEntry.tool_geometry_id = newGeom.id;
    std::string srcName = srcEntry->name;
    if (srcName.empty()) srcName = autoFormatToolName(*srcGeom);
    newEntry.name = srcName + " (Copy)";
    m_toolDatabase->insertTreeEntry(newEntry);

    // Select the new tool
    m_selectedTreeEntryId = newEntry.id;
    m_selectedGeometryId = newGeom.id;
    refresh();
}

void ToolBrowserPanel::deleteSelected() {
    if (m_selectedTreeEntryId.empty() || !m_toolDatabase) return;

    // Find the tree entry
    for (const auto& entry : m_treeEntries) {
        if (entry.id != m_selectedTreeEntryId) continue;

        if (!entry.tool_geometry_id.empty()) {
            // Delete geometry and associated entities/cutting data
            auto entities = m_toolDatabase->findEntitiesForGeometry(entry.tool_geometry_id);
            for (const auto& e : entities) {
                m_toolDatabase->removeCuttingData(e.tool_cutting_data_id);
                m_toolDatabase->removeEntity(e.id);
            }
            m_toolDatabase->removeGeometry(entry.tool_geometry_id);
        }
        m_toolDatabase->removeTreeEntry(entry.id);
        break;
    }

    m_selectedTreeEntryId.clear();
    m_selectedGeometryId.clear();
    m_hasCuttingData = false;
    refresh();
}

} // namespace dw
