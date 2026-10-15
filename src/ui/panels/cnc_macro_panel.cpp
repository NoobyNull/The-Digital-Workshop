#include "ui/panels/cnc_macro_panel.h"

#include <algorithm>
#include <cstring>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets/edit_buffer.h"

namespace dw {

CncMacroPanel::CncMacroPanel() : Panel("Macros") {}

void CncMacroPanel::onConnectionChanged(bool connected, const std::string& /*version*/) {
    m_connected = connected;
    if (!connected) {
        m_executingId = -1;
    }
}

void CncMacroPanel::onStatusUpdate(const MachineStatus& status) {
    m_machineState = status.state;
}

void CncMacroPanel::refreshMacros() {
    if (!m_macroManager) return;
    m_macros = m_macroManager->getAll();
    m_needsRefresh = false;
}

void CncMacroPanel::render() {
    if (!m_open) return;

    applyMinSize(14, 8);
    if (!ImGui::Begin(m_title.c_str(), closeHandle())) {
        ImGui::End();
        return;
    }

    if (!m_macroManager) {
        ImGui::TextDisabled("Macro manager not available");
        ImGui::End();
        return;
    }

    if (m_needsRefresh) {
        refreshMacros();
    }

    renderMacroList();

    if (m_editingId != -1) {
        ImGui::Separator();
        renderEditArea();
    }

    ImGui::End();
}

void CncMacroPanel::renderMacroList() {
    // State guards: disable execution during streaming or alarm
    bool canRun = m_connected &&
                  m_machineState != MachineState::Alarm &&
                  m_machineState != MachineState::Run &&
                  m_machineState != MachineState::Home &&
                  !m_streaming;

    ImGui::Text("Macros");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
    if (ImGui::SmallButton(Icons::Add)) {
        // Start new macro edit
        m_editingId = 0;
        m_editIsNew = true;
        clearBuffer(m_editName);
        clearBuffer(m_editGcode);
        clearBuffer(m_editShortcut);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add new macro");
    }

    ImGui::Separator();

    // Macro list with drag-and-drop reordering
    for (size_t i = 0; i < m_macros.size(); ++i) {
        auto& macro = m_macros[i];
        ImGui::PushID(macro.id);

        // Drag handle
        ImGui::Button("=##drag", ImVec2(ImGui::GetFrameHeight(), 0));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            m_dragSourceIdx = static_cast<int>(i);
            ImGui::SetDragDropPayload("MACRO_REORDER", &m_dragSourceIdx, sizeof(int));
            ImGui::Text("Move: %s", macro.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MACRO_REORDER")) {
                int srcIdx = *static_cast<const int*>(payload->Data);
                int dstIdx = static_cast<int>(i);
                if (srcIdx != dstIdx && srcIdx >= 0 &&
                    srcIdx < static_cast<int>(m_macros.size())) {
                    std::vector<int> ids;
                    for (const auto& m : m_macros) ids.push_back(m.id);
                    int movedId = ids[static_cast<size_t>(srcIdx)];
                    ids.erase(ids.begin() + srcIdx);
                    ids.insert(ids.begin() + dstIdx, movedId);
                    m_macroManager->reorder(ids);
                    m_needsRefresh = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Drag to reorder");
        }

        // Run button
        ImGui::SameLine();
        if (!canRun) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton(Icons::Play)) {
            executeMacro(macro);
        }
        if (!canRun) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!m_connected) {
                ImGui::SetTooltip("Connect to run macros");
            } else if (!canRun) {
                ImGui::SetTooltip("Cannot run macro in current state");
            } else {
                ImGui::SetTooltip("Run: %s", macro.name.c_str());
            }
        }

        // Macro name
        ImGui::SameLine();
        if (macro.builtIn) {
            ImGui::TextDisabled("%s", Icons::Settings);
            ImGui::SameLine();
        }
        ImGui::Text("%s", macro.name.c_str());

        // Shortcut hint
        if (!macro.shortcut.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", macro.shortcut.c_str());
        }

        // Edit button
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
        if (ImGui::SmallButton("Edit")) {
            m_editingId = macro.id;
            m_editIsNew = false;
            fillBuffer(m_editName, macro.name);
            fillBuffer(m_editGcode, macro.gcode);
            fillBuffer(m_editShortcut, macro.shortcut);
        }

        // Delete button (only for non-built-in)
        if (!macro.builtIn) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
            if (ImGui::SmallButton(Icons::Delete)) {
                try {
                    m_macroManager->deleteMacro(macro.id);
                    m_needsRefresh = true;
                    if (m_editingId == macro.id) {
                        m_editingId = -1;
                    }
                } catch (...) {
                    // Should not happen for non-built-in, but guard
                }
            }
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }

    if (m_macros.empty()) {
        ImGui::TextDisabled("No macros defined. Click + to add one.");
    }
}

void CncMacroPanel::renderEditArea() {
    ImGui::Text(m_editIsNew ? "New Macro" : "Edit Macro");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Name", m_editName, sizeof(m_editName));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##gcode", m_editGcode, sizeof(m_editGcode),
                              ImVec2(-1, ImGui::GetTextLineHeight() * 6));

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Shortcut", m_editShortcut, sizeof(m_editShortcut));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Optional keyboard shortcut (e.g. Ctrl+1)");
    }

    ImGui::Spacing();

    // Save button
    bool nameValid = std::strlen(m_editName) > 0;
    bool gcodeValid = std::strlen(m_editGcode) > 0;
    if (!nameValid || !gcodeValid) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save")) {
        if (m_editIsNew) {
            Macro m;
            m.name = m_editName;
            m.gcode = m_editGcode;
            m.shortcut = m_editShortcut;
            m.sortOrder = static_cast<int>(m_macros.size());
            m_macroManager->addMacro(m);
        } else {
            auto existing = m_macroManager->getById(m_editingId);
            existing.name = m_editName;
            existing.gcode = m_editGcode;
            existing.shortcut = m_editShortcut;
            m_macroManager->updateMacro(existing);
        }
        m_editingId = -1;
        m_needsRefresh = true;
    }
    if (!nameValid || !gcodeValid) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_editingId = -1;
    }
}

void CncMacroPanel::executeMacro(const Macro& macro) {
    if (!m_cnc || !m_macroManager) return;

    auto lines = m_macroManager->parseLines(macro);
    for (const auto& line : lines) {
        m_cnc->sendCommand(line);
    }
}

} // namespace dw
