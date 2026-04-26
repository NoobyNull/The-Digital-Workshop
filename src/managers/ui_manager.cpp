// Digital Workshop - UI Manager (core)
// Init, shutdown, panel rendering, background UI, panel registry.
// Menu/keyboard code → ui_manager_menus.cpp
// Layout/preset code → ui_manager_layout.cpp

#include "managers/ui_manager.h"

#include <algorithm>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>

#include "core/import/import_queue.h"
#include "core/import/import_task.h"
#include "core/config/config.h"
#include "core/threading/loading_state.h"
#include "core/utils/thread_utils.h"
#include "ui/context_menu_manager.h"
#include "ui/dialogs/dialog.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/import_options_dialog.h"
#include "ui/dialogs/import_summary_dialog.h"
#include "ui/dialogs/lighting_dialog.h"
#include "ui/dialogs/machine_profile_dialog.h"
#include "ui/dialogs/maintenance_dialog.h"
#include "ui/dialogs/message_dialog.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/dialogs/tag_image_dialog.h"
#include "ui/dialogs/settings_import_dialog.h"
#include "ui/dialogs/tagger_shutdown_dialog.h"
#include "ui/panels/cnc_console_panel.h"
#include "ui/panels/cnc_jog_panel.h"
#include "ui/panels/cnc_job_panel.h"
#include "ui/panels/cnc_macro_panel.h"
#include "ui/panels/cnc_safety_panel.h"
#include "ui/panels/cnc_settings_panel.h"
#include "ui/panels/cnc_status_panel.h"
#include "ui/panels/cnc_tool_panel.h"
#include "ui/panels/cnc_wcs_panel.h"
#include "ui/panels/cost_panel.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/direct_carve_panel.h"
#include "ui/panels/group_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/log_viewer_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/start_page.h"
#include "ui/panels/tool_browser_panel.h"
#include "ui/tool_library_access.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/status_bar.h"
#include "ui/widgets/status_tips.h"
#include "ui/widgets/toast.h"

namespace dw {

UIManager::UIManager() = default;

UIManager::~UIManager() {
    shutdown();
}

void UIManager::init(LibraryManager* libraryManager,
                     ProjectManager* projectManager,
                     MaterialManager* materialManager,
                     CostRepository* costRepo,
                     RateCategoryRepository* rateCatRepo,
                     ModelRepository* modelRepo,
                     GCodeRepository* gcodeRepo,
                     CutPlanRepository* cutPlanRepo) {
    m_viewportPanel = std::make_unique<ViewportPanel>();
    m_libraryPanel = std::make_unique<LibraryPanel>(libraryManager);
    m_propertiesPanel = std::make_unique<PropertiesPanel>();
    m_projectPanel = std::make_unique<ProjectPanel>(
        projectManager, modelRepo, gcodeRepo, cutPlanRepo, costRepo);
    m_gcodePanel = std::make_unique<GCodePanel>();
    m_logViewerPanel = std::make_unique<LogViewerPanel>();
    m_cutOptimizerPanel = std::make_unique<CutOptimizerPanel>();
    m_materialsPanel = std::make_unique<MaterialsPanel>(materialManager);
    if (costRepo)
        m_costPanel = std::make_unique<CostingPanel>(costRepo, rateCatRepo);
    m_startPage = std::make_unique<StartPage>();
    m_toolBrowserPanel = std::make_unique<ToolBrowserPanel>();
    m_cncStatusPanel = std::make_unique<CncStatusPanel>();
    m_cncJogPanel = std::make_unique<CncJogPanel>();
    m_cncConsolePanel = std::make_unique<CncConsolePanel>();
    m_cncWcsPanel = std::make_unique<CncWcsPanel>();
    m_cncToolPanel = std::make_unique<CncToolPanel>();
    m_cncJobPanel = std::make_unique<CncJobPanel>();
    m_cncSafetyPanel = std::make_unique<CncSafetyPanel>();
    m_cncSettingsPanel = std::make_unique<CncSettingsPanel>();
    m_cncMacroPanel = std::make_unique<CncMacroPanel>();
    m_directCarvePanel = std::make_unique<DirectCarvePanel>();
    m_fileDialog = std::make_unique<FileDialog>();
    m_lightingDialog = std::make_unique<LightingDialog>();
    m_machineProfileDialog = std::make_unique<MachineProfileDialog>();
    m_machineProfileDialog->setOnProfileChanged([this]() {
        for (const auto& cb : m_onMachineProfileChanged) {
            if (cb)
                cb();
        }
    });
    m_importSummaryDialog = std::make_unique<ImportSummaryDialog>();
    m_importOptionsDialog = std::make_unique<ImportOptionsDialog>();
    m_progressDialog = std::make_unique<ProgressDialog>();
    m_tagImageDialog = std::make_unique<TagImageDialog>();
    m_maintenanceDialog = std::make_unique<MaintenanceDialog>();
    m_taggerShutdownDialog = std::make_unique<TaggerShutdownDialog>();
    m_settingsImportDialog = std::make_unique<SettingsImportDialog>();
    m_statusBar = std::make_unique<StatusBar>();
    m_statusBar->setOnOpenToolLibrary([this]() { openToolLibrary(); });
    m_contextMenuManager = std::make_unique<ContextMenuManager>();

    if (m_libraryPanel)
        m_libraryPanel->setContextMenuManager(m_contextMenuManager.get());
    if (m_materialsPanel)
        m_materialsPanel->setContextMenuManager(m_contextMenuManager.get());
    if (m_viewportPanel) {
        m_viewportPanel->setContextMenuManager(m_contextMenuManager.get());
        m_viewportPanel->setSenderWorkspaceActive(m_workspaceMode == WorkspaceMode::CNC);
        m_lightingDialog->setSettings(&m_viewportPanel->renderSettings());
    }
    if (m_gcodePanel)
        m_gcodePanel->setFileDialog(m_fileDialog.get());

    buildPanelRegistry();
    m_dialogList = {
        m_fileDialog.get(),         m_importSummaryDialog.get(),
        m_importOptionsDialog.get(), m_tagImageDialog.get(),
        m_taggerShutdownDialog.get(), m_maintenanceDialog.get(),
    };
}

void UIManager::shutdown() {
    m_dialogList.clear();
    m_panelRegistry.clear();

    // Destroy dialogs
    m_fileDialog.reset();
    m_lightingDialog.reset();
    m_machineProfileDialog.reset();
    m_importSummaryDialog.reset();
    m_importOptionsDialog.reset();
    m_progressDialog.reset();
    m_tagImageDialog.reset();
    m_taggerShutdownDialog.reset();
    m_settingsImportDialog.reset();
    m_maintenanceDialog.reset();

    // Destroy widgets
    m_statusBar.reset();

    // Destroy group panels
    m_groupPanels.clear();

    // Destroy panels
    m_viewportPanel.reset();
    m_libraryPanel.reset();
    m_propertiesPanel.reset();
    m_projectPanel.reset();
    m_gcodePanel.reset();
    m_logViewerPanel.reset();
    m_cutOptimizerPanel.reset();
    m_costPanel.reset();
    m_materialsPanel.reset();
    m_toolBrowserPanel.reset();
    m_cncStatusPanel.reset();
    m_cncJogPanel.reset();
    m_cncConsolePanel.reset();
    m_cncWcsPanel.reset();
    m_cncToolPanel.reset();
    m_cncJobPanel.reset();
    m_cncSafetyPanel.reset();
    m_cncSettingsPanel.reset();
    m_cncMacroPanel.reset();
    m_directCarvePanel.reset();
    m_startPage.reset();
}

void UIManager::buildPanelRegistry() {
    // key, showFlag, menuLabel, windowTitle, panel*, syncClose
    // syncClose: panels that pass p_open to ImGui::Begin and need X-button sync
    m_panelRegistry = {
        {"start_page",      &m_showStartPage,      "Start Page",        "Start Page",
         m_startPage.get(), false, WindowRole::Workshop},
        {"viewport",        &m_showViewport,        "Viewport",          "Viewport",
         m_viewportPanel.get(), false, WindowRole::Shared},
        {"library",         &m_showLibrary,         "Library",           "Library",
         m_libraryPanel.get(), false, WindowRole::Workshop},
        {"properties",      &m_showProperties,      "Properties",        "Properties",
         m_propertiesPanel.get(), false, WindowRole::Workshop},
        {"project",         &m_showProject,         "Project",           "Project",
         m_projectPanel.get(), false, WindowRole::Shared},
        {"gcode",           &m_showGCode,           "G-code Viewer",     "G-code",
         m_gcodePanel.get(), false, WindowRole::Sender},
        {"log_viewer",      &m_showLogViewer,       "Log Viewer",        "Log Viewer",
         m_logViewerPanel.get(), true, WindowRole::Shared},
        {"cut_optimizer",   &m_showCutOptimizer,    "Cut Optimizer",     "Cut Optimizer",
         m_cutOptimizerPanel.get(), false, WindowRole::Workshop},
        {"project_costing", &m_showProjectCosting,  "Project Costing",   "Project Costing",
         m_costPanel.get(), true, WindowRole::Workshop},
        {"materials",       &m_showMaterials,       "Materials",         "Materials",
         m_materialsPanel.get(), true, WindowRole::Workshop},
        {"tool_browser",    &m_showToolBrowser,     kToolLibraryMenuLabel, kToolLibraryWindowTitle,
         m_toolBrowserPanel.get(), true, WindowRole::Shared},
        {"cnc_status",      &m_showCncStatus,       "Status",            "CNC Status",
         m_cncStatusPanel.get(), true, WindowRole::Sender},
        {"cnc_jog",         &m_showCncJog,          "Jog Control",       "Jog Control",
         m_cncJogPanel.get(), true, WindowRole::Sender},
        {"cnc_console",     &m_showCncConsole,      "MDI Console",       "MDI Console",
         m_cncConsolePanel.get(), true, WindowRole::Sender},
        {"cnc_wcs",         &m_showCncWcs,          "Work Zero / WCS",   "WCS",
         m_cncWcsPanel.get(), true, WindowRole::Sender},
        {"cnc_tool",        &m_showCncTool,         "Tool & Material",   "Tool & Material",
         m_cncToolPanel.get(), true, WindowRole::Sender},
        {"cnc_job",         &m_showCncJob,          "Job Progress",      "Job Progress",
         m_cncJobPanel.get(), true, WindowRole::Sender},
        {"cnc_safety",      &m_showCncSafety,       "Safety Controls",   "Safety",
         m_cncSafetyPanel.get(), true, WindowRole::Sender},
        {"cnc_settings",    &m_showCncSettings,     "Machine Settings",  "Machine Settings",
         m_cncSettingsPanel.get(), true, WindowRole::Sender},
        {"cnc_macros",      &m_showCncMacros,       "Macros",            "Macros",
         m_cncMacroPanel.get(), true, WindowRole::Sender},
        {"direct_carve",    &m_showDirectCarve,     "Direct Carve",      "Direct Carve",
         m_directCarvePanel.get(), true, WindowRole::Sender},
    };
}

void UIManager::renderPanels() {
    ASSERT_MAIN_THREAD();

    // Reset auto-context guard each frame
    m_suppressAutoContext = false;

    // Render all visible panels via registry
    for (auto& entry : m_panelRegistry) {
        if (!*entry.showFlag || !entry.panel)
            continue;

        const bool focusToolLibrary =
            m_focusToolLibraryNextFrame && std::strcmp(entry.key, "tool_browser") == 0;
        if (focusToolLibrary)
            ImGui::SetNextWindowFocus();
        entry.panel->render();
        if (focusToolLibrary)
            m_focusToolLibraryNextFrame = false;

        // Sync X-button close back to visibility flag
        if (entry.syncClose && !entry.panel->isOpen()) {
            *entry.showFlag = false;
            entry.panel->setOpen(true);
        }
    }

    // Render group panels and check for close → layout reset
    bool groupClosed = false;
    for (auto& gp : m_groupPanels) {
        gp->render();
        if (gp->wasClosed())
            groupClosed = true;
    }
    if (groupClosed) {
        m_groupPanels.clear();
        ImGuiID dockId = ImGui::GetID("MainDockSpace");
        setupDefaultDockLayout(dockId);
    }

    // Auto-context: detect focused panel and trigger preset switch
    if (!m_suppressAutoContext) {
        ImGuiWindow* navWin = GImGui->NavWindow;
        if (navWin) {
            const char* focusedName = navWin->Name;
            for (const auto& entry : m_panelRegistry) {
                if (*entry.showFlag && std::strcmp(focusedName, entry.windowTitle) == 0) {
                    checkAutoContextTrigger(entry.key);
                    break;
                }
            }
        }
    }

    // Render dialogs
    for (auto* dialog : m_dialogList) {
        if (dialog)
            dialog->render();
    }

    // LightingDialog doesn't inherit from Dialog — render separately
    if (m_lightingDialog)
        m_lightingDialog->render();

    // MachineProfileDialog is a global service window shared by all panels.
    if (m_machineProfileDialog)
        m_machineProfileDialog->render();
}

void UIManager::renderBackgroundUI(float deltaTime, const LoadingState* loadingState) {
    if (m_statusBar) {
        m_statusBar->setContextTips(currentStatusTips());
        m_statusBar->render(loadingState);
    }

    if (m_progressDialog)
        m_progressDialog->render();

    MessageDialog::renderGlobal();
    SavePromptDialog::renderGlobal();
    ToastManager::instance().render(deltaTime);

    if (m_settingsImportDialog)
        m_settingsImportDialog->render();
}

std::vector<std::string> UIManager::currentStatusTips() const {
    auto& cfg = Config::instance();
    StatusTipState state;
    state.hasLoadedModel = m_viewportPanel != nullptr && m_viewportPanel->hasValidModel();
    state.cncConnected = m_cncConnected;
    state.cncStreaming = m_cncStreaming;
    state.lightDirection = cfg.getBinding(BindAction::LightDirDrag);
    state.lightIntensity = cfg.getBinding(BindAction::LightIntensityDrag);
    state.feedOverridePlus = cfg.getBinding(BindAction::FeedOverridePlus);
    state.feedOverrideMinus = cfg.getBinding(BindAction::FeedOverrideMinus);
    state.spindleOverridePlus = cfg.getBinding(BindAction::SpindleOverridePlus);
    state.spindleOverrideMinus = cfg.getBinding(BindAction::SpindleOverrideMinus);

    if (m_workspaceMode == WorkspaceMode::CNC) {
        return buildStatusTips(StatusTipContext::Sender, state);
    }
    if (m_showViewport) {
        return buildStatusTips(StatusTipContext::WorkshopViewport, state);
    }
    return buildStatusTips(StatusTipContext::Workshop, state);
}

void UIManager::setImportProgress(const ImportProgress* progress) {
    if (m_statusBar)
        m_statusBar->setImportProgress(progress);
}

void UIManager::setTaggerProgress(const TaggerProgress* progress) {
    if (m_statusBar)
        m_statusBar->setTaggerProgress(progress);
}

void UIManager::showImportSummary(const ImportBatchSummary& summary) {
    if (m_importSummaryDialog)
        m_importSummaryDialog->open(summary);
}

void UIManager::setImportCancelCallback(std::function<void()> callback) {
    if (m_statusBar)
        m_statusBar->setOnCancel(std::move(callback));
}

void UIManager::setTaggerCancelCallback(std::function<void()> callback) {
    if (m_statusBar)
        m_statusBar->setOnCancelTagging(std::move(callback));
}

void UIManager::showTaggerShutdownDialog(const TaggerProgress* progress) {
    if (m_taggerShutdownDialog)
        m_taggerShutdownDialog->open(progress);
}

SettingsImportDialog* UIManager::settingsImportDialog() const {
    return m_settingsImportDialog.get();
}

void UIManager::addGroupPanel() {
    m_groupPanels.push_back(std::make_unique<GroupPanel>(m_nextGroupId++));
}

void UIManager::showCncPanels(bool show) {
    for (auto& entry : m_panelRegistry) {
        if (entry.role == WindowRole::Sender)
            *entry.showFlag = show;
    }
}

void UIManager::setCncStreaming(bool v) {
    m_cncStreaming = v;
    if (m_cncStreaming && m_workspaceMode != WorkspaceMode::CNC) {
        setWorkspaceMode(WorkspaceMode::CNC);
    }
}

void UIManager::setWorkspaceMode(WorkspaceMode mode) {
    if (m_cncStreaming && mode != WorkspaceMode::CNC) {
        return;
    }

    m_workspaceMode = mode;
    syncWorkspaceModeToPanels();
    applyLayoutPreset(mode == WorkspaceMode::CNC ? 1 : 0);
}

void UIManager::enforceWorkspaceBoundary() {
    const WindowRole hiddenRole =
        m_workspaceMode == WorkspaceMode::CNC ? WindowRole::Workshop : WindowRole::Sender;
    for (auto& entry : m_panelRegistry) {
        if (entry.role == hiddenRole)
            *entry.showFlag = false;
    }

    m_showViewport = true;
    if (m_workspaceMode == WorkspaceMode::CNC && !senderSurfaceVisible()) {
        m_showGCode = true;
        m_showCncStatus = true;
        m_showCncJob = true;
        m_showCncSafety = true;
    }
}

void UIManager::syncWorkspaceModeToPanels() {
    if (m_viewportPanel) {
        m_viewportPanel->setSenderWorkspaceActive(m_workspaceMode == WorkspaceMode::CNC);
    }
}

bool UIManager::senderSurfaceVisible() const {
    return m_showGCode || m_showCncStatus || m_showCncJob || m_showCncSafety ||
           m_showCncJog || m_showCncConsole || m_showCncWcs || m_showCncTool ||
           m_showCncSettings || m_showCncMacros || m_showDirectCarve;
}

bool UIManager::isWindowVisible(const std::string& key) const {
    const auto* catalog = findWindowCatalogEntry(key);
    const std::string canonical = catalog ? catalog->key : key;

    if (canonical == "machine_profiles")
        return m_machineProfileDialog && m_machineProfileDialog->isOpen();
    if (canonical == "lighting_settings")
        return m_lightingDialog && m_lightingDialog->isOpen();

    const std::string layoutKey =
        catalog && !catalog->layoutKey.empty() ? catalog->layoutKey : key;
    for (const auto& entry : m_panelRegistry) {
        if (entry.key == layoutKey || (catalog && catalog->matchesKey(entry.key)))
            return *entry.showFlag;
    }
    return false;
}

void UIManager::openWindow(const std::string& key) {
    const auto* catalog = findWindowCatalogEntry(key);
    const std::string canonical = catalog ? catalog->key : key;

    if (canonical == "machine_profiles") {
        openMachineProfiles();
        return;
    }
    if (canonical == "lighting_settings") {
        if (m_lightingDialog)
            m_lightingDialog->open();
        return;
    }

    const std::string layoutKey =
        catalog && !catalog->layoutKey.empty() ? catalog->layoutKey : key;
    auto it = std::find_if(m_panelRegistry.begin(), m_panelRegistry.end(),
                           [&](const auto& entry) {
                               return entry.key == layoutKey ||
                                      (catalog && catalog->matchesKey(entry.key));
                           });
    if (it == m_panelRegistry.end())
        return;
    if (!it->panel)
        return;

    if (it->role == WindowRole::Sender && m_workspaceMode != WorkspaceMode::CNC) {
        setWorkspaceMode(WorkspaceMode::CNC);
    } else if (it->role == WindowRole::Workshop && m_workspaceMode != WorkspaceMode::Model) {
        if (m_cncStreaming)
            return;
        setWorkspaceMode(WorkspaceMode::Model);
    }

    *it->showFlag = true;
    if (it->panel)
        it->panel->setOpen(true);
    ImGui::SetWindowFocus(it->windowTitle);
}

void UIManager::toggleWindow(const std::string& key) {
    const auto* catalog = findWindowCatalogEntry(key);
    const std::string canonical = catalog ? catalog->key : key;

    if (canonical == "machine_profiles") {
        if (m_machineProfileDialog && m_machineProfileDialog->isOpen())
            m_machineProfileDialog->close();
        else
            openMachineProfiles();
        return;
    }
    if (canonical == "lighting_settings") {
        if (m_lightingDialog) {
            if (m_lightingDialog->isOpen())
                m_lightingDialog->close();
            else
                m_lightingDialog->open();
        }
        return;
    }

    const std::string layoutKey =
        catalog && !catalog->layoutKey.empty() ? catalog->layoutKey : key;
    for (auto& entry : m_panelRegistry) {
        if (entry.key != layoutKey && !(catalog && catalog->matchesKey(entry.key)))
            continue;

        if (*entry.showFlag) {
            *entry.showFlag = false;
        } else {
            openWindow(key);
        }
        return;
    }
}

void UIManager::openToolLibrary() {
    openWindow("tool_library");
    m_focusToolLibraryNextFrame = true;
}

void UIManager::openMachineProfiles() {
    if (m_machineProfileDialog)
        m_machineProfileDialog->open();
    ImGui::SetWindowFocus("Machine Profiles");
}

void UIManager::addMachineProfileChangedCallback(ActionCallback cb) {
    m_onMachineProfileChanged.push_back(std::move(cb));
}

int64_t UIManager::getSelectedModelId() const {
    if (m_libraryPanel)
        return m_libraryPanel->selectedModelId();
    return -1;
}

} // namespace dw
