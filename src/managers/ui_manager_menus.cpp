// Digital Workshop - UI Manager (menus & keyboard shortcuts)
// Menu bar rendering, about/restart dialogs, keyboard shortcut dispatch.

#include "managers/ui_manager.h"

#include <algorithm>
#include <array>

#include <imgui.h>

#include "core/config/config.h"
#include "core/paths/app_paths.h"
#include "ui/dialogs/lighting_dialog.h"
#include "ui/icons.h"
#include "ui/panels/cnc_jog_panel.h"
#include "ui/tool_library_access.h"
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
    renderResetToDefaultsDialog();
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
            ? "The workspace stays fixed while a G-code stream is active."
            : "Workshop / Projects (Ctrl+1)");
    }

    ImGui::SameLine(0, 0);
    const bool lockSender = senderActive || m_showLibrary || m_cncStreaming;
    if (lockSender)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Sender"))
        setWorkspaceMode(WorkspaceMode::CNC);
    if (lockSender)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(m_cncStreaming
                              ? "The workspace stays fixed while a G-code stream is active."
                              : (m_showLibrary
                                     ? "Return from the Design Library before opening CNC Sender."
                                     : "CNC Sender (Ctrl+2)"));
    }
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
    if (ImGui::MenuItem("Import Models...", "Ctrl+I") && m_onImportModel)
        m_onImportModel();
    if (ImGui::MenuItem("Import Folder...") && m_onImportFolder)
        m_onImportFolder();
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
    if (ImGui::MenuItem("Reset to Defaults...")) {
        m_resetToDefaultsError.clear();
        m_showResetToDefaultsPopup = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit", "Alt+F4") && m_onQuit)
        m_onQuit();
    ImGui::EndMenu();
}

void UIManager::renderViewMenu() {
    if (!ImGui::BeginMenu("View"))
        return;

    if (ImGui::BeginMenu("Experience")) {
        const bool guided = guidedExperienceSelected();
        if (m_cncStreaming)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem("Guided Workshop", nullptr, guided))
            selectGuidedExperience(true);
        if (ImGui::MenuItem("Advanced Workbench", nullptr, !guided))
            selectGuidedExperience(false);
        if (m_cncStreaming)
            ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    ImGui::Separator();

    bool isModel = (m_workspaceMode == WorkspaceMode::Model);
    bool isCnc = (m_workspaceMode == WorkspaceMode::CNC);
    if (m_cncStreaming)
        ImGui::BeginDisabled();
    if (ImGui::MenuItem("Workshop", "Ctrl+1", isModel))
        setWorkspaceMode(WorkspaceMode::Model);
    if (m_cncStreaming) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The workspace stays fixed while a G-code stream is active.");
    }
    const bool lockSenderWorkspace = m_showLibrary || m_cncStreaming;
    if (lockSenderWorkspace)
        ImGui::BeginDisabled();
    if (ImGui::MenuItem("CNC Sender", "Ctrl+2", isCnc))
        setWorkspaceMode(WorkspaceMode::CNC);
    if (lockSenderWorkspace) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(m_cncStreaming
                                  ? "The workspace stays fixed while a G-code stream is active."
                                  : "Return from the Design Library before opening CNC Sender.");
        }
    }
    ImGui::Separator();

    if (ImGui::MenuItem("Viewport", nullptr, isWindowVisible("viewport")))
        toggleWindow("viewport");
    if (ImGui::MenuItem("Project", nullptr, isWindowVisible("project")))
        toggleWindow("project");
    if (ImGui::MenuItem(kToolLibraryMenuLabel, nullptr, isWindowVisible("tool_library")))
        toggleWindow("tool_library");
    ImGui::Separator();

    if (m_workspaceMode == WorkspaceMode::Model) {
        if (ImGui::MenuItem("Home", nullptr, isWindowVisible("start_page")))
            toggleWindow("start_page");
        renderDesignLibraryMenuItem();
        if (ImGui::MenuItem("Properties", nullptr, isWindowVisible("properties")))
            toggleWindow("properties");
        ImGui::Separator();
        if (ImGui::MenuItem("Cut Optimizer", nullptr, isWindowVisible("cut_optimizer")))
            toggleWindow("cut_optimizer");
        if (ImGui::MenuItem("Project Costing", nullptr, isWindowVisible("project_costing")))
            toggleWindow("project_costing");
        if (ImGui::MenuItem("Materials", nullptr, isWindowVisible("materials")))
            toggleWindow("materials");
    } else if (ImGui::BeginMenu("Sender Panels")) {
        renderSenderSubmenu();
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Add Group Panel"))
        addGroupPanel();
    ImGui::Separator();
    if (ImGui::MenuItem("Log Viewer", "Ctrl+Alt+Shift+L", isWindowVisible("log_viewer")))
        toggleWindow("log_viewer");
    if (ImGui::MenuItem("Lighting Settings", "Ctrl+L", isWindowVisible("lighting_settings")))
        toggleWindow("lighting_settings");
    ImGui::EndMenu();
}

void UIManager::renderSenderSubmenu() {
    if (ImGui::MenuItem("G-code Viewer", nullptr, isWindowVisible("gcode_viewer")))
        toggleWindow("gcode_viewer");
    ImGui::Separator();
    if (ImGui::MenuItem("Status", nullptr, isWindowVisible("cnc_status")))
        toggleWindow("cnc_status");
    if (ImGui::MenuItem("Jog Control", nullptr, isWindowVisible("cnc_jog")))
        toggleWindow("cnc_jog");
    if (ImGui::MenuItem("MDI Console", nullptr, isWindowVisible("cnc_console")))
        toggleWindow("cnc_console");
    if (ImGui::MenuItem("Work Zero / WCS", nullptr, isWindowVisible("cnc_wcs")))
        toggleWindow("cnc_wcs");
    ImGui::Separator();
    if (ImGui::MenuItem("Tool & Material", nullptr, isWindowVisible("runtime_tool_setup")))
        toggleWindow("runtime_tool_setup");
    if (ImGui::MenuItem("Job Progress", nullptr, isWindowVisible("cnc_job")))
        toggleWindow("cnc_job");
    if (ImGui::MenuItem("Safety Controls", nullptr, isWindowVisible("cnc_safety")))
        toggleWindow("cnc_safety");
    ImGui::Separator();
    if (ImGui::MenuItem("Machine Settings", nullptr, isWindowVisible("machine_settings")))
        toggleWindow("machine_settings");
    if (ImGui::MenuItem("Macros", nullptr, isWindowVisible("cnc_macros")))
        toggleWindow("cnc_macros");
    ImGui::Separator();
    if (ImGui::MenuItem("CAM", nullptr, isWindowVisible("direct_carve")))
        toggleWindow("direct_carve");
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
    if (ImGui::MenuItem("Start Background AI Tagging") && m_onStartBackgroundTagging)
        m_onStartBackgroundTagging();
    if (ImGui::MenuItem("Stop Background AI Tagging") && m_onStopBackgroundTagging)
        m_onStopBackgroundTagging();
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

void UIManager::renderResetToDefaultsDialog() {
    if (m_showResetToDefaultsPopup)
        ImGui::OpenPopup("Reset to Defaults");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(
            "Reset to Defaults", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "This will remove all Digital Workshop settings, databases, cache files, "
            "and the default workspace folders. The app will close after reset.");
        ImGui::Spacing();
        ImGui::Text("Directories to delete:");
        ImGui::Indent();
        for (const auto& target : paths::getFactoryResetTargets()) {
            ImGui::BulletText("%s", target.string().c_str());
        }
        ImGui::Unindent();

        if (!m_resetToDefaultsError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                               "%s",
                               m_resetToDefaultsError.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset and Close")) {
            if (m_onResetToDefaults) {
                m_resetToDefaultsError = m_onResetToDefaults();
                if (m_resetToDefaultsError.empty()) {
                    m_showResetToDefaultsPopup = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_resetToDefaultsError.clear();
            m_showResetToDefaultsPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void UIManager::renderAboutDialog() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "About Digital Workshop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Digital Workshop");
        ImGui::Text("Version %s", VERSION);
        ImGui::TextDisabled("Build %s", GIT_HASH);
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

    if (io.KeyAlt && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L)) {
        toggleWindow("log_viewer");
        return;
    }

    if (!io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L))
        openWindow("lighting_settings");

    if (ImGui::IsKeyPressed(ImGuiKey_1) && !m_cncStreaming)
        setWorkspaceMode(WorkspaceMode::Model);
    if (ImGui::IsKeyPressed(ImGuiKey_2) && !m_cncStreaming)
        setWorkspaceMode(WorkspaceMode::CNC);
}

} // namespace dw
