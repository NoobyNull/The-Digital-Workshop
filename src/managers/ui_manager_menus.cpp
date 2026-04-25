// Digital Workshop - UI Manager (menus & keyboard shortcuts)
// Menu bar rendering, about/restart dialogs, keyboard shortcut dispatch.

#include "managers/ui_manager.h"

#include <algorithm>
#include <array>

#include <imgui.h>

#include "core/config/config.h"
#include "ui/dialogs/lighting_dialog.h"
#include "ui/icons.h"
#include "ui/panels/cnc_jog_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/ui_colors.h"
#include "version.h"

namespace dw {

void UIManager::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        renderFileMenu();
        renderViewMenu();
        renderEditMenu();
        renderToolsMenu();
        renderHelpMenu();

        renderToolbar();

        renderPresetSelector();
        renderCncMenuBarStatus();
        ImGui::EndMainMenuBar();
    }
    renderSavePresetPopup();
}

void UIManager::renderToolbar() {
    // Separator between menus and toolbar icons
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("|");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2.0f);

    float btnH = ImGui::GetFrameHeight();

    // New Project
    if (ImGui::Button(Icons::New, ImVec2(btnH, btnH)) && m_onNewProject)
        m_onNewProject();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New Project (Ctrl+N)");

    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);

    // Save Project
    if (ImGui::Button(Icons::Save, ImVec2(btnH, btnH)) && m_onSaveProject)
        m_onSaveProject();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save Project (Ctrl+S)");

    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("|");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2.0f);

    const bool workshopActive = m_workspaceMode == WorkspaceMode::Model;
    const bool senderActive = m_workspaceMode == WorkspaceMode::CNC;
    const bool lockWorkshop = workshopActive || m_cncStreaming;

    if (lockWorkshop)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Workshop"))
        setWorkspaceMode(WorkspaceMode::Model);
    if (lockWorkshop)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(m_cncStreaming
            ? "Finish or stop the active G-code stream before leaving Sender."
            : "Workshop / Projects (Ctrl+1)");
    }

    ImGui::SameLine(0, 0);
    if (senderActive)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Sender"))
        setWorkspaceMode(WorkspaceMode::CNC);
    if (senderActive)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("CNC Sender (Ctrl+2)");
}

void UIManager::renderCncMenuBarStatus() {
    float barWidth = ImGui::GetWindowWidth();
    float cursorX = barWidth;

    if (m_cncConnected && !m_cncSimulating) {
        const char* label = "Disconnect";
        const auto& style = ImGui::GetStyle();
        float btnWidth = ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f;
        cursorX -= btnWidth + style.FramePadding.x;
        ImGui::SetCursorPosX(cursorX);
        if (ImGui::SmallButton(label) && m_onDisconnect)
            m_onDisconnect();
        return;
    }

    if (!m_cncSimulating)
        return;

    if (!m_availablePorts.empty()) {
        const char* connLabel = "Connect";
        const auto& style = ImGui::GetStyle();
        float connWidth = ImGui::CalcTextSize(connLabel).x + style.FramePadding.x * 4.0f;
        cursorX -= connWidth + style.FramePadding.x;
        ImGui::SetCursorPosX(cursorX);
        if (ImGui::SmallButton(connLabel)) {
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
    }

    const char* label = "Virtual CNC";
    float textWidth = ImGui::CalcTextSize(label).x;
    cursorX -= textWidth + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(cursorX);
    ImGui::PushStyleColor(ImGuiCol_Text, colors::kDimmed);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void UIManager::renderFileMenu() {
    if (!ImGui::BeginMenu("File"))
        return;

    if (ImGui::MenuItem("New Project", "Ctrl+N") && m_onNewProject)
        m_onNewProject();
    if (ImGui::MenuItem("Open Project", "Ctrl+O") && m_onOpenProject)
        m_onOpenProject();
    if (ImGui::MenuItem("Save Project", "Ctrl+S") && m_onSaveProject)
        m_onSaveProject();
    ImGui::Separator();
    if (ImGui::MenuItem("Import Model", "Ctrl+I") && m_onImportModel)
        m_onImportModel();
    if (ImGui::MenuItem("Export Model", "Ctrl+E") && m_onExportModel)
        m_onExportModel();
    ImGui::Separator();
    if (ImGui::MenuItem("Import .dwproj...") && m_onImportProjectArchive)
        m_onImportProjectArchive();
    ImGui::Separator();
    if (ImGui::MenuItem("Export Settings...") && m_onExportSettings)
        m_onExportSettings();
    if (ImGui::MenuItem("Import Settings...") && m_onImportSettings)
        m_onImportSettings();
    ImGui::Separator();
    if (ImGui::MenuItem("Exit", "Alt+F4") && m_onQuit)
        m_onQuit();
    ImGui::EndMenu();
}

void UIManager::renderViewMenu() {
    if (!ImGui::BeginMenu("View"))
        return;

    bool isModel = (m_workspaceMode == WorkspaceMode::Model);
    bool isCnc = (m_workspaceMode == WorkspaceMode::CNC);
    if (m_cncStreaming)
        ImGui::BeginDisabled();
    if (ImGui::MenuItem("Workshop", "Ctrl+1", isModel))
        setWorkspaceMode(WorkspaceMode::Model);
    if (m_cncStreaming) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Finish or stop the active G-code stream before leaving Sender.");
    }
    if (ImGui::MenuItem("CNC Sender", "Ctrl+2", isCnc))
        setWorkspaceMode(WorkspaceMode::CNC);
    ImGui::Separator();

    ImGui::MenuItem("Viewport", nullptr, &m_showViewport);
    ImGui::MenuItem("Project", nullptr, &m_showProject);
    ImGui::Separator();

    if (m_workspaceMode == WorkspaceMode::Model) {
        ImGui::MenuItem("Start Page", nullptr, &m_showStartPage);
        ImGui::MenuItem("Library", nullptr, &m_showLibrary);
        ImGui::MenuItem("Properties", nullptr, &m_showProperties);
        ImGui::Separator();
        ImGui::MenuItem("Cut Optimizer", nullptr, &m_showCutOptimizer);
        ImGui::MenuItem("Project Costing", nullptr, &m_showProjectCosting);
        ImGui::MenuItem("Materials", nullptr, &m_showMaterials);
    } else if (ImGui::BeginMenu("Sender Panels")) {
        renderSenderSubmenu();
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Add Group Panel"))
        addGroupPanel();
    ImGui::Separator();
    if (ImGui::MenuItem("Lighting Settings", "Ctrl+L") && m_lightingDialog)
        m_lightingDialog->open();
    ImGui::EndMenu();
}

void UIManager::renderSenderSubmenu() {
    ImGui::MenuItem("G-code Viewer", nullptr, &m_showGCode);
    ImGui::Separator();
    ImGui::MenuItem("Status", nullptr, &m_showCncStatus);
    ImGui::MenuItem("Jog Control", nullptr, &m_showCncJog);
    ImGui::MenuItem("MDI Console", nullptr, &m_showCncConsole);
    ImGui::MenuItem("Work Zero / WCS", nullptr, &m_showCncWcs);
    ImGui::Separator();
    ImGui::MenuItem("Tool & Material", nullptr, &m_showCncTool);
    ImGui::MenuItem("Job Progress", nullptr, &m_showCncJob);
    ImGui::MenuItem("Safety Controls", nullptr, &m_showCncSafety);
    ImGui::Separator();
    ImGui::MenuItem("Firmware Settings", nullptr, &m_showCncSettings);
    ImGui::MenuItem("Macros", nullptr, &m_showCncMacros);
    ImGui::Separator();
    ImGui::MenuItem("Direct Carve", nullptr, &m_showDirectCarve);
    ImGui::Separator();
    if (ImGui::BeginMenu("Live Overlay")) {
        auto& cfg = Config::instance();
        bool dot = cfg.getCncShowToolDot();
        if (ImGui::MenuItem("Tool Position", nullptr, &dot)) cfg.setCncShowToolDot(dot);
        bool env = cfg.getCncShowWorkEnvelope();
        if (ImGui::MenuItem("Work Envelope", nullptr, &env)) cfg.setCncShowWorkEnvelope(env);
        bool dro = cfg.getCncShowDroOverlay();
        if (ImGui::MenuItem("Position Readout", nullptr, &dro)) cfg.setCncShowDroOverlay(dro);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Show All"))
        showCncPanels(true);
    if (m_cncStreaming)
        ImGui::BeginDisabled();
    if (ImGui::MenuItem("Hide All"))
        showCncPanels(false);
    if (m_cncStreaming) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Sender panels stay visible while a G-code stream is active.");
    }

    if (m_workspaceMode == WorkspaceMode::CNC)
        enforceWorkspaceBoundary();
}

void UIManager::renderEditMenu() {
    if (!ImGui::BeginMenu("Edit"))
        return;

    if (ImGui::MenuItem("Settings", "Ctrl+,") && m_onSpawnSettings)
        m_onSpawnSettings();
    ImGui::EndMenu();
}

void UIManager::renderToolsMenu() {
    if (!ImGui::BeginMenu("Tools"))
        return;

    bool hasViewportModel = m_viewportPanel != nullptr && m_viewportPanel->hasValidModel();
    if (!hasViewportModel) {
        ImGui::BeginDisabled();
    }
    if (ImGui::MenuItem("Recalculate Model Normals") && m_viewportPanel) {
        m_viewportPanel->recalculateModelNormals();
    }
    if (!hasViewportModel) {
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Library Maintenance...") && m_onLibraryMaintenance)
        m_onLibraryMaintenance();
    if (ImGui::MenuItem("Relocate Workspace...") && m_onRelocateWorkspace)
        m_onRelocateWorkspace();
    if (ImGui::MenuItem("Locate Missing Files...") && m_onLocateMissingFiles)
        m_onLocateMissingFiles();
    ImGui::EndMenu();
}

void UIManager::renderHelpMenu() {
    if (!ImGui::BeginMenu("Help"))
        return;

    if (ImGui::MenuItem("About Digital Workshop"))
        ImGui::OpenPopup("About Digital Workshop");
    ImGui::EndMenu();
}

void UIManager::renderAboutDialog() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "About Digital Workshop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Digital Workshop");
        ImGui::Text("Version %s", VERSION);
        ImGui::Separator();
        ImGui::TextWrapped("A 3D model management application for CNC and 3D printing workflows.");
        ImGui::Spacing();
        ImGui::Text("Libraries:");
        ImGui::BulletText("SDL2 - Window management");
        ImGui::BulletText("Dear ImGui - User interface");
        ImGui::BulletText("OpenGL 3.3 - 3D rendering");
        ImGui::BulletText("SQLite3 - Database");
        ImGui::Separator();
        ImGui::TextDisabled("Built with C++17");
        ImGui::Spacing();
        float okBtnW = ImGui::CalcTextSize("OK").x + ImGui::GetStyle().FramePadding.x * 6;
        if (ImGui::Button("OK", ImVec2(okBtnW, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void UIManager::renderRestartPopup(const ActionCallback& onRelaunch) {
    if (!m_showRestartPopup)
        return;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Restart Required",
                     &m_showRestartPopup,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("UI Scale has been changed.");
        ImGui::Text("A restart is required to apply this setting.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const auto& style = ImGui::GetStyle();
        float restartBtnW = ImGui::CalcTextSize("Relaunch Now").x + style.FramePadding.x * 4;
        float laterBtnW = ImGui::CalcTextSize("Later").x + style.FramePadding.x * 4;
        if (ImGui::Button("Relaunch Now", ImVec2(restartBtnW, 0))) {
            m_showRestartPopup = false;
            if (onRelaunch)
                onRelaunch();
        }
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(laterBtnW, 0)))
            m_showRestartPopup = false;
    }
    ImGui::End();
}

bool UIManager::checkPanicStop() {
    if (!m_cncStreaming || !m_cncConnected || m_cncSimulating)
        return false;

    for (int k = static_cast<int>(ImGuiKey_NamedKey_BEGIN);
         k < static_cast<int>(ImGuiKey_NamedKey_END); ++k) {
        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
            continue;

        float now = static_cast<float>(ImGui::GetTime());
        m_panicKeyTimes[m_panicKeyHead] = now;
        m_panicKeyHead = (m_panicKeyHead + 1) % PANIC_KEY_COUNT;
        float oldest = m_panicKeyTimes[m_panicKeyHead];
        if (oldest > 0.0f && (now - oldest) <= PANIC_WINDOW_SEC) {
            if (m_onPanicStop) m_onPanicStop();
            std::fill(std::begin(m_panicKeyTimes), std::end(m_panicKeyTimes), 0.0f);
            return true;
        }
        break; // only record one key per frame
    }
    return false;
}

void UIManager::handleKeyboardShortcuts() {
    // Panic stop runs BEFORE WantTextInput — works even in MDI console
    if (checkPanicStop())
        return;

    auto& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;

    if (m_showCncJog && m_cncJogPanel)
        m_cncJogPanel->handleKeyboardJog();

    if (!io.KeyCtrl)
        return;

    struct Shortcut {
        ImGuiKey key;
        const ActionCallback* action;
    };
    const std::array<Shortcut, 6> kShortcuts = {{
        {ImGuiKey_N, &m_onNewProject},
        {ImGuiKey_O, &m_onOpenProject},
        {ImGuiKey_S, &m_onSaveProject},
        {ImGuiKey_I, &m_onImportModel},
        {ImGuiKey_E, &m_onExportModel},
        {ImGuiKey_Comma, &m_onSpawnSettings},
    }};

    for (const auto& [key, action] : kShortcuts) {
        if (ImGui::IsKeyPressed(key) && *action) {
            (*action)();
            return;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_L) && m_lightingDialog)
        m_lightingDialog->open();

    if (ImGui::IsKeyPressed(ImGuiKey_1) && !m_cncStreaming)
        setWorkspaceMode(WorkspaceMode::Model);
    if (ImGui::IsKeyPressed(ImGuiKey_2))
        setWorkspaceMode(WorkspaceMode::CNC);
}

} // namespace dw
