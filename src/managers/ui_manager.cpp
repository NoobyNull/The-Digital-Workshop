// Digital Workshop - UI Manager (core)
// Init, shutdown, panel registry, panel rendering, and main tab selection.
// Top bar → ui_manager_top_bar.cpp
// Menu/keyboard code → ui_manager_menus.cpp
// Page host and per-tab layouts → ui_manager_layout.cpp

#include "managers/ui_manager.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include "core/config/config.h"
#include "core/import/import_queue.h"
#include "core/import/import_task.h"
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
#include "ui/dialogs/settings_import_dialog.h"
#include "ui/dialogs/supplier_tool_import_dialog.h"
#include "ui/dialogs/tag_image_dialog.h"
#include "ui/dialogs/tagger_shutdown_dialog.h"
#include "ui/panels/cnc_console_panel.h"
#include "ui/panels/cnc_job_panel.h"
#include "ui/panels/cnc_jog_panel.h"
#include "ui/panels/cnc_macro_panel.h"
#include "ui/panels/cnc_safety_panel.h"
#include "ui/panels/cnc_settings_panel.h"
#include "ui/panels/cnc_status_panel.h"
#include "ui/panels/cnc_tool_panel.h"
#include "ui/panels/cnc_wcs_panel.h"
#include "ui/panels/cam_placeholder_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/log_viewer_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/start_page.h"
#include "ui/panels/tool_browser_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/tool_library_access.h"
#include "ui/widgets/status_bar.h"
#include "ui/widgets/status_tips.h"
#include "ui/widgets/toast.h"

namespace dw {

UIManager::UIManager() = default;

UIManager::~UIManager() {
    shutdown();
}

void UIManager::init(LibraryManager* libraryManager,
                     ProjectManager* /*projectManager*/,
                     MaterialManager* materialManager,
                     ModelRepository* /*modelRepo*/,
                     GCodeRepository* /*gcodeRepo*/) {
    m_viewportPanel = std::make_unique<ViewportPanel>();
    m_libraryPanel = std::make_unique<LibraryPanel>(libraryManager);
    m_propertiesPanel = std::make_unique<PropertiesPanel>();
    m_projectPanel = std::make_unique<ProjectPanel>();
    m_gcodePanel = std::make_unique<GCodePanel>();
    m_logViewerPanel = std::make_unique<LogViewerPanel>();
    m_materialsPanel = std::make_unique<MaterialsPanel>(materialManager);
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
    m_camPlaceholderPanel = std::make_unique<CamPlaceholderPanel>();
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
    m_supplierToolImportDialog = std::make_unique<SupplierToolImportDialog>();
    m_statusBar = std::make_unique<StatusBar>();
    m_statusBar->setOnOpenToolLibrary([this]() { openToolLibrary(); });
    m_contextMenuManager = std::make_unique<ContextMenuManager>();

    if (m_libraryPanel)
        m_libraryPanel->setContextMenuManager(m_contextMenuManager.get());
    if (m_materialsPanel)
        m_materialsPanel->setContextMenuManager(m_contextMenuManager.get());
    if (m_viewportPanel) {
        m_viewportPanel->setContextMenuManager(m_contextMenuManager.get());
        m_lightingDialog->setSettings(&m_viewportPanel->renderSettings());
    }
    if (m_gcodePanel)
        m_gcodePanel->setFileDialog(m_fileDialog.get());

    buildPanelRegistry();
    (void)selectTab(workshop::MainTab::Home);
    m_dialogList = {
        m_fileDialog.get(),
        m_importSummaryDialog.get(),
        m_importOptionsDialog.get(),
        m_tagImageDialog.get(),
        m_taggerShutdownDialog.get(),
        m_maintenanceDialog.get(),
        m_supplierToolImportDialog.get(),
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
    m_supplierToolImportDialog.reset();
    m_maintenanceDialog.reset();

    // Destroy widgets
    m_statusBar.reset();

    // Destroy panels
    m_viewportPanel.reset();
    m_libraryPanel.reset();
    m_propertiesPanel.reset();
    m_projectPanel.reset();
    m_gcodePanel.reset();
    m_logViewerPanel.reset();
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
    m_camPlaceholderPanel.reset();
    m_startPage.reset();
}

void UIManager::buildPanelRegistry() {
    // key, showFlag, windowTitle, panel*, syncClose, floating
    // syncClose: panels that pass p_open to ImGui::Begin and need X-button sync
    // clang-format off
    // Order is the tab order inside each tabbed page section.
    m_panelRegistry = {
        {"start_page",   &m_showStartPage,   "Home###Start Page", m_startPage.get(),           true,  false},
        {"viewport",     &m_showViewport,    "Viewport",          m_viewportPanel.get(),       false, false},
        {"library",      &m_showLibrary,     "Library",           m_libraryPanel.get(),        false, false},
        {"project",      &m_showProject,     "Project",           m_projectPanel.get(),        false, false},
        {"gcode",        &m_showGCode,       "G-code",            m_gcodePanel.get(),          false, false},
        {"direct_carve", &m_showDirectCarve, "CAM",               m_camPlaceholderPanel.get(), true,  false},
        {"properties",   &m_showProperties,  "Properties",        m_propertiesPanel.get(),     false, false},
        {"cnc_status",   &m_showCncStatus,   "CNC Status",        m_cncStatusPanel.get(),      true,  false},
        {"cnc_jog",      &m_showCncJog,      "Jog Control",       m_cncJogPanel.get(),         true,  false},
        {"cnc_safety",   &m_showCncSafety,   "Safety",            m_cncSafetyPanel.get(),      true,  false},
        {"cnc_wcs",      &m_showCncWcs,      "WCS",               m_cncWcsPanel.get(),         true,  false},
        {"cnc_tool",     &m_showCncTool,     "Tool & Material",   m_cncToolPanel.get(),        true,  false},
        {"cnc_macros",   &m_showCncMacros,   "Macros",            m_cncMacroPanel.get(),       true,  false},
        {"cnc_settings", &m_showCncSettings, "Machine Settings",  m_cncSettingsPanel.get(),    true,  false},
        {"cnc_job",      &m_showCncJob,      "Job Progress",      m_cncJobPanel.get(),         true,  false},
        {"cnc_console",  &m_showCncConsole,  "MDI Console",       m_cncConsolePanel.get(),     true,  false},
        {"materials",    &m_showMaterials,   "Materials",         m_materialsPanel.get(),      true,  true},
        {"tool_browser", &m_showToolBrowser, kToolLibraryWindowTitle, m_toolBrowserPanel.get(), true, true},
        {"log_viewer",   &m_showLogViewer,   "Log Viewer",        m_logViewerPanel.get(),      true,  true},
    };
    // clang-format on
    for (auto& entry : m_panelRegistry) {
        if (entry.panel)
            entry.panel->setPageWindow(!entry.floating);
    }
}

const UIManager::PanelEntry* UIManager::findPanel(const std::string& key) const {
    const auto* catalog = findWindowCatalogEntry(key);
    const std::string layoutKey = catalog && !catalog->layoutKey.empty() ? catalog->layoutKey : key;
    for (const auto& entry : m_panelRegistry) {
        if (entry.key == layoutKey || (catalog && catalog->matchesKey(entry.key)))
            return &entry;
    }
    return nullptr;
}

UIManager::PanelEntry* UIManager::findPanel(const std::string& key) {
    return const_cast<PanelEntry*>(std::as_const(*this).findPanel(key));
}

void UIManager::renderPanels() {
    ASSERT_MAIN_THREAD();

    const auto* viewport = ImGui::GetMainViewport();
    for (auto& entry : m_panelRegistry) {
        if (!*entry.showFlag || !entry.panel)
            continue;

        if (entry.floating) {
            // Floating windows open centered over the page they were called
            // from; the user may move them afterwards.
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                                    ImVec2(0.5F, 0.5F));
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.6F,
                                            viewport->WorkSize.y * 0.7F),
                                     ImGuiCond_FirstUseEver);
        }
        const bool focusToolLibrary = m_focusToolLibraryNextFrame &&
                                      std::strcmp(entry.key, "tool_browser") == 0;
        if (focusToolLibrary)
            ImGui::SetNextWindowFocus();
        entry.panel->render();
        if (focusToolLibrary)
            m_focusToolLibraryNextFrame = false;

        // A page window has no close button; only floating windows close.
        if (entry.syncClose && !entry.panel->isOpen()) {
            if (entry.floating)
                *entry.showFlag = false;
            entry.panel->setOpen(true);
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

    applyPendingSectionTabs();
}

SettingsImportDialog* UIManager::settingsImportDialog() const {
    return m_settingsImportDialog.get();
}

workshop::MainTabFacts UIManager::tabFacts() const {
    workshop::MainTabFacts facts;
    facts.active = m_activeTab;
    facts.machineStreaming = m_cncStreaming;
    facts.gcodeLoaded = m_gcodePanel && m_gcodePanel->hasGCode();
    facts.toolpathGenerationAvailable = false;
    return facts;
}

bool UIManager::selectTab(workshop::MainTab tab) {
    if (workshop::mainTabBlock(tab, tabFacts()) != workshop::MainTabBlock::None)
        return false;
    if (tab != m_activeTab)
        m_layoutDirty = true;
    m_activeTab = tab;
    applyTabToPanels();
    return true;
}

void UIManager::requestTab(workshop::MainTab tab) {
    if (workshop::mainTabBlock(tab, tabFacts()) != workshop::MainTabBlock::None)
        return;
    if (m_onMainTabRequested)
        m_onMainTabRequested(tab);
    else
        (void)selectTab(tab);
}

void UIManager::setCncStreaming(bool streaming) {
    m_cncStreaming = streaming;
    if (streaming && !isMachineWorkspace())
        (void)selectTab(workshop::MainTab::Device);
}

void UIManager::applyTabToPanels() {
    const auto& keys = pageWindowKeys(m_activeTab);
    for (auto& entry : m_panelRegistry) {
        if (entry.floating)
            continue;
        *entry.showFlag = std::any_of(keys.begin(), keys.end(), [&](const char* key) {
            return std::strcmp(key, entry.key) == 0;
        });
    }
    if (m_viewportPanel)
        m_viewportPanel->setSenderWorkspaceActive(isMachineWorkspace());
    // Preview inspects G-code and Device streams it, like a slicer's Preview
    // and Device tabs.
    if (m_gcodePanel) {
        m_gcodePanel->setMode(isMachineWorkspace() ? GCodePanelMode::Send
                                                   : GCodePanelMode::View);
    }
}

bool UIManager::isWindowVisible(const std::string& key) const {
    const auto* catalog = findWindowCatalogEntry(key);
    const std::string canonical = catalog ? catalog->key : key;

    if (canonical == "machine_profiles")
        return m_machineProfileDialog && m_machineProfileDialog->isOpen();
    if (canonical == "lighting_settings")
        return m_lightingDialog && m_lightingDialog->isOpen();

    const auto* entry = findPanel(key);
    return entry && *entry->showFlag;
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

    auto* entry = findPanel(key);
    if (!entry || !entry->panel)
        return;
    if (!entry->floating && !*entry->showFlag &&
        !selectTab(defaultTabForWindow(entry->key)))
        return;

    *entry->showFlag = true;
    entry->panel->setOpen(true);
    ImGui::SetWindowFocus(entry->windowTitle);
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

    auto* entry = findPanel(key);
    if (!entry)
        return;
    // Page windows belong to their tab; only floating windows toggle.
    if (entry->floating && *entry->showFlag)
        *entry->showFlag = false;
    else
        openWindow(key);
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
