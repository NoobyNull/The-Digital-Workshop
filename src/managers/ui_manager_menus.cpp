// Digital Workshop - UI Manager (menus & keyboard shortcuts)
// Menus shown from the top bar's menu button, about/restart dialogs, and
// keyboard shortcut dispatch.

#include "managers/ui_manager.h"

#include <algorithm>
#include <array>
#include <string>

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

    const auto facts = tabFacts();
    int shortcut = 1;
    for (const auto tab : workshop::kMainTabs) {
        const auto block = workshop::mainTabBlock(tab, facts);
        const std::string keys = "Ctrl+" + std::to_string(shortcut++);
        if (block != workshop::MainTabBlock::None)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(workshop::mainTabLabel(tab), keys.c_str(), tab == m_activeTab))
            requestTab(tab);
        if (block != workshop::MainTabBlock::None) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", workshop::blockReason(block));
        }
    }
    ImGui::Separator();

    if (ImGui::MenuItem("Materials", nullptr, isWindowVisible("materials")))
        toggleWindow("materials");
    if (ImGui::MenuItem(kToolLibraryMenuLabel, nullptr, isWindowVisible("tool_library")))
        toggleWindow("tool_library");
    if (ImGui::MenuItem("Machine Profiles", nullptr, isWindowVisible("machine_profiles")))
        toggleWindow("machine_profiles");
    ImGui::Separator();
    renderLiveOverlayMenu();
    ImGui::Separator();
    if (ImGui::MenuItem("Log Viewer", "Ctrl+Alt+Shift+L", isWindowVisible("log_viewer")))
        toggleWindow("log_viewer");
    if (ImGui::MenuItem("Lighting Settings", "Ctrl+L", isWindowVisible("lighting_settings")))
        toggleWindow("lighting_settings");
    ImGui::EndMenu();
}

void UIManager::renderLiveOverlayMenu() {
    if (!ImGui::BeginMenu("Device Overlay"))
        return;
    auto& cfg = Config::instance();
    bool dot = cfg.getCncShowToolDot();
    if (ImGui::MenuItem("Tool Position", nullptr, &dot))
        cfg.setCncShowToolDot(dot);
    bool env = cfg.getCncShowWorkEnvelope();
    if (ImGui::MenuItem("Work Envelope", nullptr, &env))
        cfg.setCncShowWorkEnvelope(env);
    bool dro = cfg.getCncShowDroOverlay();
    if (ImGui::MenuItem("Position Readout", nullptr, &dro))
        cfg.setCncShowDroOverlay(dro);
    ImGui::EndMenu();
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
        m_openAboutRequested = true;
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
    // Opened here rather than inside the menu so the popup shares this ID scope.
    if (m_openAboutRequested) {
        ImGui::OpenPopup("About Digital Workshop");
        m_openAboutRequested = false;
    }
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

    if (isMachineWorkspace() && m_cncJogPanel)
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

    // Ctrl+1..6 select the main tabs in order, like a slicer's tab bar.
    constexpr std::array<ImGuiKey, workshop::kMainTabs.size()> kTabKeys = {
        ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6};
    for (std::size_t index = 0; index < kTabKeys.size(); ++index) {
        if (ImGui::IsKeyPressed(kTabKeys[index], false)) {
            requestTab(workshop::kMainTabs[index]);
            return;
        }
    }
}

} // namespace dw
