// Digital Workshop - UI Manager (top bar)
// The slicer-style title strip: menu button and Save on the left, the main
// tabs, the project name, then machine status and the primary actions
// (Generate and Send to Machine) on the right, like Bambu Studio's
// Slice plate and Print plate.

#include "managers/ui_manager.h"

#include <algorithm>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "modules/workshop/ui/project_shell_presentation.h"
#include "ui/icons.h"
#include "ui/panels/gcode_panel.h"
#include "ui/ui_colors.h"

namespace dw {
namespace {

using workshop::MainTab;
using workshop::PrimaryAction;
using workshop::PrimaryActionBlock;

// Font Awesome solid glyphs beyond the shared icon table.
constexpr const char* kIconMenu = "\xef\x83\x89";     // f0c9 bars
constexpr const char* kIconLibrary = "\xef\x80\xad";  // f02d book
constexpr const char* kIconPrepare = "\xef\x86\xb2";  // f1b2 cube
constexpr const char* kIconPreview = "\xef\x81\xae";  // f06e eye
constexpr const char* kIconDevice = "\xef\x8b\x9b";   // f2db microchip
constexpr const char* kIconGenerate = "\xef\x82\x85"; // f085 gears
constexpr const char* kIconSend = "\xef\x87\x98";     // f1d8 paper-plane

const char* tabIcon(MainTab tab) {
    switch (tab) {
    case MainTab::Home:
        return Icons::Home;
    case MainTab::Library:
        return kIconLibrary;
    case MainTab::Prepare:
        return kIconPrepare;
    case MainTab::Preview:
        return kIconPreview;
    case MainTab::Device:
        return kIconDevice;
    case MainTab::Project:
        return Icons::Project;
    }
    return Icons::Home;
}

std::string actionLabel(PrimaryAction action) {
    const char* icon = action == PrimaryAction::GenerateToolpaths ? kIconGenerate : kIconSend;
    return std::string(icon) + "  " + workshop::primaryActionLabel(action);
}

std::string tabButtonLabel(MainTab tab, bool iconOnly) {
    std::string label = tabIcon(tab);
    if (!iconOnly) {
        label += "  ";
        label += workshop::mainTabLabel(tab);
    }
    label += "##MainTab";
    label += workshop::mainTabLabel(tab);
    return label;
}

float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0F;
}

const workshop::ProjectShellSnapshot& emptyShellSnapshot() {
    static const workshop::ProjectShellSnapshot snapshot(
        {}, {}, workshop::MachineStatusSnapshot{"Machine", false, false});
    return snapshot;
}

} // namespace

void UIManager::setProjectShellSnapshot(workshop::ProjectShellSnapshot snapshot) {
    m_shellSnapshot = std::move(snapshot);
}

void UIManager::renderTopBar() {
    auto* viewport = ImGui::GetMainViewport();
    const auto& style = ImGui::GetStyle();
    const float buttonH = ImGui::GetFrameHeight() * 1.25F;
    const float barH = buttonH + style.WindowPadding.y * 2.0F;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##TopBar", viewport, ImGuiDir_Up, barH, flags)) {
        const auto& snapshot = m_shellSnapshot ? *m_shellSnapshot : emptyShellSnapshot();
        const auto presentation = workshop::projectShellPresentation(snapshot);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x * 1.5F,
                                   (buttonH - ImGui::GetFontSize()) * 0.5F));
        renderAppMenuButton();

        // Measure the right-hand group first so the tabs and project name can
        // give way on narrow windows instead of overlapping it.
        const float gap = style.ItemSpacing.x;
        const float machineW = ImGui::CalcTextSize(presentation.machineLabel.c_str()).x;
        const float actionsW =
            buttonWidth(actionLabel(PrimaryAction::GenerateToolpaths).c_str()) +
            buttonWidth(actionLabel(PrimaryAction::SendToMachine).c_str()) + gap;
        float connectionW = 0.0F;
        if (m_cncConnected && !m_cncSimulating)
            connectionW = buttonWidth("Disconnect") + gap;
        else if (m_cncSimulating && !m_availablePorts.empty())
            connectionW = buttonWidth("Connect") + gap;
        const float rightW = machineW + gap + connectionW + actionsW;
        const float available = ImGui::GetContentRegionAvail().x - rightW;

        float fullTabsW = 0.0F;
        for (const auto tab : workshop::kMainTabs)
            fullTabsW += buttonWidth(tabButtonLabel(tab, false).c_str()) + gap;
        const bool iconsOnly = fullTabsW > available;
        renderTabButtons(iconsOnly);

        // Project name and save state sit centered in the space that is left,
        // where a slicer shows its project title.
        std::string title = presentation.projectLabel;
        const float titleW = ImGui::CalcTextSize(title.c_str()).x +
                             (presentation.projectDirty
                                  ? gap + ImGui::CalcTextSize("Unsaved").x
                                  : 0.0F);
        const float afterTabs = ImGui::GetCursorPosX();
        const float rightStart = ImGui::GetWindowWidth() - style.WindowPadding.x - rightW;
        if (rightStart - afterTabs > titleW + gap * 4.0F) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(afterTabs + (rightStart - afterTabs - titleW) * 0.5F);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(title.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Project: %s", presentation.projectLabel.c_str());
            if (presentation.projectDirty) {
                ImGui::SameLine();
                ImGui::TextColored(colors::kWarning, "Unsaved");
            }
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightStart));
        renderMachineStatus();
        renderPrimaryActions();
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    renderResetToDefaultsDialog();
}

void UIManager::renderAppMenuButton() {
    const float side = ImGui::GetFrameHeight();
    std::string menuLabel = std::string(kIconMenu) + "##AppMenu";
    if (ImGui::Button(menuLabel.c_str(), ImVec2(side, side)))
        ImGui::OpenPopup("##AppMenuPopup");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Menu");
    if (ImGui::BeginPopup("##AppMenuPopup")) {
        renderFileMenu();
        renderEditMenu();
        renderViewMenu();
        renderToolsMenu();
        renderHelpMenu();
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    std::string saveLabel = std::string(Icons::Save) + "##SaveProject";
    if (ImGui::Button(saveLabel.c_str(), ImVec2(side, side)) && m_onSaveProject)
        m_onSaveProject();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save Project (Ctrl+S)");

    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * 3.0F);
}

void UIManager::renderTabButtons(bool iconsOnly) {
    const auto facts = tabFacts();
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const float underline = std::max(2.0F, ImGui::GetFontSize() * 0.15F);
    int shortcut = 1;
    for (const auto tab : workshop::kMainTabs) {
        const bool active = tab == m_activeTab;
        const auto block = workshop::mainTabBlock(tab, facts);
        const std::string label = tabButtonLabel(tab, iconsOnly);

        ImGui::PushStyleColor(ImGuiCol_Button,
                              active ? ImGui::GetStyleColorVec4(ImGuiCol_TabActive)
                                     : ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        if (block != workshop::MainTabBlock::None)
            ImGui::BeginDisabled();
        if (ImGui::Button(label.c_str()) && !active)
            requestTab(tab);
        if (block != workshop::MainTabBlock::None)
            ImGui::EndDisabled();
        ImGui::PopStyleColor();

        // The selected tab is underlined as well as filled, so it never
        // depends on color alone.
        if (active) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(min.x, max.y - underline), max, ImGui::GetColorU32(accent));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (block != workshop::MainTabBlock::None)
                ImGui::SetTooltip("%s", workshop::blockReason(block));
            else
                ImGui::SetTooltip("%s (Ctrl+%d)", workshop::mainTabLabel(tab), shortcut);
        }
        ++shortcut;
        ImGui::SameLine();
    }
}

void UIManager::renderMachineStatus() {
    const auto& snapshot = m_shellSnapshot ? *m_shellSnapshot : emptyShellSnapshot();
    const auto presentation = workshop::projectShellPresentation(snapshot);

    ImGui::AlignTextToFramePadding();
    const ImVec4 color = presentation.machineRunning
                             ? colors::kWarning
                             : (snapshot.machineStatus().connected ? colors::kSuccess
                                                                   : colors::kDimmed);
    ImGui::TextColored(color, "%s", presentation.machineLabel.c_str());
    ImGui::SameLine();

    if (m_cncConnected && !m_cncSimulating) {
        if (ImGui::SmallButton("Disconnect") && m_onDisconnect)
            m_onDisconnect();
        ImGui::SameLine();
    } else if (m_cncSimulating && !m_availablePorts.empty()) {
        if (ImGui::SmallButton("Connect")) {
            if (m_availablePorts.size() == 1 && m_onConnect)
                m_onConnect(m_availablePorts.front());
            else
                ImGui::OpenPopup("##PortSelect");
        }
        if (ImGui::BeginPopup("##PortSelect")) {
            for (const auto& port : m_availablePorts) {
                if (ImGui::MenuItem(port.c_str()) && m_onConnect)
                    m_onConnect(port);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    }
}

void UIManager::renderPrimaryActions() {
    const auto facts = tabFacts();
    const auto& style = ImGui::GetStyle();

    bool first = true;
    for (const auto action : {PrimaryAction::GenerateToolpaths, PrimaryAction::SendToMachine}) {
        const auto block = workshop::primaryActionBlock(action, facts);
        const bool blocked = block != PrimaryActionBlock::None;
        const std::string label = actionLabel(action);
        // Ready actions are filled with the accent; blocked ones fall back to
        // a plain button so they never look clickable.
        ImGui::PushStyleColor(ImGuiCol_Button, blocked ? style.Colors[ImGuiCol_Button]
                                                       : style.Colors[ImGuiCol_ButtonActive]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_ButtonHovered]);
        ImGui::PushStyleColor(ImGuiCol_Text, blocked ? style.Colors[ImGuiCol_TextDisabled]
                                                     : ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
        if (!first)
            ImGui::SameLine();
        first = false;
        if (blocked)
            ImGui::BeginDisabled();
        if (ImGui::Button(label.c_str()) && action == PrimaryAction::SendToMachine)
            sendToMachine();
        if (blocked)
            ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (blocked)
                ImGui::SetTooltip("%s", workshop::blockReason(block));
            else if (action == PrimaryAction::SendToMachine)
                ImGui::SetTooltip("Open the loaded G-code in Device, ready to run.");
            else
                ImGui::SetTooltip("Generate toolpaths for the selected CAM setup.");
        }
    }
}

void UIManager::sendToMachine() {
    if (workshop::primaryActionBlock(PrimaryAction::SendToMachine, tabFacts()) !=
        PrimaryActionBlock::None)
        return;
    requestTab(MainTab::Device);
}

} // namespace dw
