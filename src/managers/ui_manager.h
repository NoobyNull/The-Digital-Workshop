#pragma once

// Digital Workshop - UI Manager
// Owns all UI panels, dialogs, the slicer-style shell (top bar and one fixed
// page layout per main tab), keyboard shortcuts, import progress overlay, and
// about/restart popups. Extracted from Application (god class decomposition).
//
// Implementation split across:
//   ui_manager.cpp          — init, shutdown, panel registry, tab selection
//   ui_manager_top_bar.cpp  — top bar: menu button, tabs, project, actions
//   ui_manager_menus.cpp    — menus, keyboard shortcuts, about/restart dialogs
//   ui_manager_layout.cpp   — page host, per-tab dock layouts, config

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../core/config/window_catalog.h"
#include "../core/types.h"
#include "../modules/workshop/main_tabs.h"
#include "../modules/workshop/project_workshop_controller.h"

using ImGuiID = unsigned int;

namespace dw {

// Forward declarations - base classes
class Panel;
class Dialog;

// Forward declarations - panels
class CncStatusPanel;
class CncJogPanel;
class CncConsolePanel;
class CncWcsPanel;
class CncToolPanel;
class CncJobPanel;
class CncSafetyPanel;
class CncSettingsPanel;
class CncMacroPanel;
class ViewportPanel;
class LibraryPanel;
class PropertiesPanel;
class ProjectPanel;
class GCodePanel;
class LogViewerPanel;
class MaterialsPanel;
class StartPage;
class ToolBrowserPanel;
class CamPlaceholderPanel;

// Forward declarations - dialogs
class FileDialog;
class LightingDialog;
class ImportSummaryDialog;
class ImportOptionsDialog;
class MachineProfileDialog;
class ProgressDialog;
class TagImageDialog;
class MaintenanceDialog;
class TaggerShutdownDialog;
class SettingsImportDialog;
class SupplierToolImportDialog;

// Forward declarations - core
struct LoadingState;
struct TaggerProgress;
struct ImportBatchSummary;
struct ImportProgress;
class LibraryManager;
class ProjectManager;
class MaterialManager;
class ModelRepository;
class GCodeRepository;
class ImportQueue;
class Workspace;

// Forward declarations - widgets
class StatusBar;
class ContextMenuManager;

// Callback types for actions that remain in Application
using ActionCallback = std::function<void()>;
using ModelIdCallback = std::function<void(int64_t)>;
using PathCallback = std::function<void(const Path&)>;
using PathsCallback = std::function<void(const std::vector<std::string>&)>;
using ResetToDefaultsCallback = std::function<std::string()>;

class UIManager {
  public:
    UIManager();
    ~UIManager();

    // Disable copy/move
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;
    UIManager(UIManager&&) = delete;
    UIManager& operator=(UIManager&&) = delete;

    // Initialize all panels and dialogs.
    // Does NOT wire StartPage callbacks (Application does that after
    // both UIManager and FileIOManager exist).
    void init(LibraryManager* libraryManager,
              ProjectManager* projectManager,
              MaterialManager* materialManager,
              ModelRepository* modelRepo = nullptr,
              GCodeRepository* gcodeRepo = nullptr);

    // Shutdown and destroy all UI resources
    void shutdown();

    // --- Per-frame rendering ---
    // Order: shortcuts, top bar, page host, panels, background UI.
    void renderTopBar();
    void renderPageHost();
    void renderPanels();
    void renderAboutDialog();
    void renderResetToDefaultsDialog();
    void renderRestartPopup(const ActionCallback& onRelaunch);
    void handleKeyboardShortcuts();
    void setProjectShellSnapshot(workshop::ProjectShellSnapshot snapshot);

    // --- Background UI (StatusBar, ToastManager, ImportSummaryDialog) ---
    void renderBackgroundUI(float deltaTime, const LoadingState* loadingState);

    // --- Main tabs (Home, Library, Prepare, Preview, Device, Project) ---
    // Each tab shows a fixed page, like a 3D-printing slicer. selectTab shows
    // the page directly and returns false when the tab policy blocks it;
    // requestTab is a user's tab click, which Application routes through the
    // project and Library flows before it selects the tab.
    [[nodiscard]] workshop::MainTab activeTab() const { return m_activeTab; }
    bool selectTab(workshop::MainTab tab);
    void requestTab(workshop::MainTab tab);
    void setOnMainTabRequested(std::function<void(workshop::MainTab)> cb) {
        m_onMainTabRequested = std::move(cb);
    }
    [[nodiscard]] bool isMachineWorkspace() const { return workshop::isMachineTab(m_activeTab); }

    // --- Panel accessors (Application needs these for wiring callbacks) ---
    ViewportPanel* viewportPanel() { return m_viewportPanel.get(); }
    LibraryPanel* libraryPanel() { return m_libraryPanel.get(); }
    PropertiesPanel* propertiesPanel() { return m_propertiesPanel.get(); }
    ProjectPanel* projectPanel() { return m_projectPanel.get(); }
    GCodePanel* gcodePanel() { return m_gcodePanel.get(); }
    LogViewerPanel* logViewerPanel() { return m_logViewerPanel.get(); }
    MaterialsPanel* materialsPanel() { return m_materialsPanel.get(); }
    StartPage* startPage() { return m_startPage.get(); }
    ToolBrowserPanel* toolBrowserPanel() { return m_toolBrowserPanel.get(); }
    CncStatusPanel* cncStatusPanel() { return m_cncStatusPanel.get(); }
    CncJogPanel* cncJogPanel() { return m_cncJogPanel.get(); }
    CncConsolePanel* cncConsolePanel() { return m_cncConsolePanel.get(); }
    CncWcsPanel* cncWcsPanel() { return m_cncWcsPanel.get(); }
    CncToolPanel* cncToolPanel() { return m_cncToolPanel.get(); }
    CncJobPanel* cncJobPanel() { return m_cncJobPanel.get(); }
    CncSafetyPanel* cncSafetyPanel() { return m_cncSafetyPanel.get(); }
    CncSettingsPanel* cncSettingsPanel() { return m_cncSettingsPanel.get(); }
    CncMacroPanel* cncMacroPanel() { return m_cncMacroPanel.get(); }
    CamPlaceholderPanel* camPlaceholderPanel() { return m_camPlaceholderPanel.get(); }
    FileDialog* fileDialog() { return m_fileDialog.get(); }
    LightingDialog* lightingDialog() { return m_lightingDialog.get(); }
    ImportSummaryDialog* importSummaryDialog() { return m_importSummaryDialog.get(); }
    ImportOptionsDialog* importOptionsDialog() { return m_importOptionsDialog.get(); }
    ProgressDialog* progressDialog() { return m_progressDialog.get(); }
    TagImageDialog* tagImageDialog() { return m_tagImageDialog.get(); }
    MaintenanceDialog* maintenanceDialog() { return m_maintenanceDialog.get(); }
    TaggerShutdownDialog* taggerShutdownDialog() { return m_taggerShutdownDialog.get(); }
    SupplierToolImportDialog* supplierToolImportDialog() {
        return m_supplierToolImportDialog.get();
    }
    ContextMenuManager* contextMenuManager() { return m_contextMenuManager.get(); }

    // --- Windows ---
    // Page windows follow the active tab. openWindow on a page window selects
    // a tab that shows it; on a floating window (Materials, Tool Library, Log
    // Viewer, dialogs) it opens and focuses that window.
    void openWindow(const std::string& key);
    void toggleWindow(const std::string& key);
    [[nodiscard]] bool isWindowVisible(const std::string& key) const;
    void openToolLibrary();
    void openMachineProfiles();
    void addMachineProfileChangedCallback(ActionCallback cb);
    bool& showRestartPopup() { return m_showRestartPopup; }

    // --- Action callbacks (set by Application) ---
    void setOnNewProject(ActionCallback cb) { m_onNewProject = std::move(cb); }
    void setOnOpenProject(ActionCallback cb) { m_onOpenProject = std::move(cb); }
    void setOnSaveProject(ActionCallback cb) { m_onSaveProject = std::move(cb); }
    void setOnImportModel(ActionCallback cb) { m_onImportModel = std::move(cb); }
    void setOnImportFolder(ActionCallback cb) { m_onImportFolder = std::move(cb); }
    void setOnExportModel(ActionCallback cb) { m_onExportModel = std::move(cb); }
    void setOnImportProjectArchive(ActionCallback cb) { m_onImportProjectArchive = std::move(cb); }
    void setOnQuit(ActionCallback cb) { m_onQuit = std::move(cb); }
    void setOnSpawnSettings(ActionCallback cb) { m_onSpawnSettings = std::move(cb); }
    void setOnShowAbout(ActionCallback cb) { m_onShowAbout = std::move(cb); }
    void setOnLibraryMaintenance(ActionCallback cb) { m_onLibraryMaintenance = std::move(cb); }
    void setOnStartBackgroundTagging(ActionCallback cb) {
        m_onStartBackgroundTagging = std::move(cb);
    }
    void setOnStopBackgroundTagging(ActionCallback cb) {
        m_onStopBackgroundTagging = std::move(cb);
    }
    void setOnRelocateWorkspace(ActionCallback cb) { m_onRelocateWorkspace = std::move(cb); }
    void setOnLocateMissingFiles(ActionCallback cb) { m_onLocateMissingFiles = std::move(cb); }
    void setOnExportSettings(ActionCallback cb) { m_onExportSettings = std::move(cb); }
    void setOnImportSettings(ActionCallback cb) { m_onImportSettings = std::move(cb); }
    void setOnResetToDefaults(ResetToDefaultsCallback cb) { m_onResetToDefaults = std::move(cb); }

    // Settings import dialog
    SettingsImportDialog* settingsImportDialog() const;

    // CNC menu bar callbacks
    using ConnectCallback = std::function<void(const std::string& port)>;
    void setOnConnect(ConnectCallback cb) { m_onConnect = std::move(cb); }
    void setOnDisconnect(ActionCallback cb) { m_onDisconnect = std::move(cb); }
    void setOnPanicStop(ActionCallback cb) { m_onPanicStop = std::move(cb); }

    // CNC streaming state. A stream holds the Device tab until it ends.
    void setCncStreaming(bool streaming);

    // CNC state for menu bar display
    void setCncConnected(bool v) { m_cncConnected = v; }
    void setCncSimulating(bool v) { m_cncSimulating = v; }
    void setAvailablePorts(std::vector<std::string> ports) { m_availablePorts = std::move(ports); }

    // --- Import progress callbacks ---
    void setImportProgress(const ImportProgress* progress);
    void setTaggerProgress(const TaggerProgress* progress);
    void showImportSummary(const ImportBatchSummary& summary);
    void setImportCancelCallback(std::function<void()> callback);
    void setTaggerCancelCallback(std::function<void()> callback);

    // Show tagger shutdown dialog
    void showTaggerShutdownDialog(const TaggerProgress* progress);

    // --- Workspace state save/restore helpers ---
    void restoreVisibilityFromConfig();
    void saveVisibilityToConfig();
    void applyRenderSettingsFromConfig();
    [[nodiscard]] int64_t getSelectedModelId() const;

  private:
    // UI Panels
    std::unique_ptr<ViewportPanel> m_viewportPanel;
    std::unique_ptr<LibraryPanel> m_libraryPanel;
    std::unique_ptr<PropertiesPanel> m_propertiesPanel;
    std::unique_ptr<ProjectPanel> m_projectPanel;
    std::unique_ptr<GCodePanel> m_gcodePanel;
    std::unique_ptr<LogViewerPanel> m_logViewerPanel;
    std::unique_ptr<MaterialsPanel> m_materialsPanel;
    std::unique_ptr<StartPage> m_startPage;
    std::unique_ptr<ToolBrowserPanel> m_toolBrowserPanel;
    std::unique_ptr<CncStatusPanel> m_cncStatusPanel;
    std::unique_ptr<CncJogPanel> m_cncJogPanel;
    std::unique_ptr<CncConsolePanel> m_cncConsolePanel;
    std::unique_ptr<CncWcsPanel> m_cncWcsPanel;
    std::unique_ptr<CncToolPanel> m_cncToolPanel;
    std::unique_ptr<CncJobPanel> m_cncJobPanel;
    std::unique_ptr<CncSafetyPanel> m_cncSafetyPanel;
    std::unique_ptr<CncSettingsPanel> m_cncSettingsPanel;
    std::unique_ptr<CncMacroPanel> m_cncMacroPanel;
    std::unique_ptr<CamPlaceholderPanel> m_camPlaceholderPanel;

    // Panel visibility
    bool m_showViewport = true;
    bool m_showLibrary = true;
    bool m_showProperties = true;
    bool m_showProject = true;
    bool m_showGCode = false;
    bool m_showLogViewer = false;
    bool m_showMaterials = false;
    bool m_showToolBrowser = false;
    bool m_showCncStatus = false;
    bool m_showCncJog = false;
    bool m_showCncConsole = false;
    bool m_showCncWcs = false;
    bool m_showCncTool = false;
    bool m_showCncJob = false;
    bool m_showCncSafety = false;
    bool m_showCncSettings = false;
    bool m_showCncMacros = false;
    bool m_showDirectCarve = false;
    bool m_showStartPage = true;

    // Panel registry — maps panel keys to visibility and rendering. Page
    // windows are shown by the active tab; floating windows by the user.
    struct PanelEntry {
        const char* key;         // Window key (e.g. "cnc_status")
        bool* showFlag;          // &m_showCncStatus
        const char* windowTitle; // ImGui window title, used to dock the window
        Panel* panel;            // Base pointer for render() dispatch
        bool syncClose;          // Handle X-button close → visibility sync
        bool floating;           // Opened by the user rather than by a tab
    };
    std::vector<PanelEntry> m_panelRegistry;
    void buildPanelRegistry();
    [[nodiscard]] const PanelEntry* findPanel(const std::string& key) const;
    [[nodiscard]] PanelEntry* findPanel(const std::string& key);
    void applyTabToPanels();
    [[nodiscard]] workshop::MainTabFacts tabFacts() const;
    std::vector<std::string> currentStatusTips() const;

    // Dialog list for batch rendering
    std::vector<Dialog*> m_dialogList;

    // Main tab state
    workshop::MainTab m_activeTab = workshop::MainTab::Home;
    bool m_layoutDirty = true;
    std::function<void(workshop::MainTab)> m_onMainTabRequested;
    std::optional<workshop::ProjectShellSnapshot> m_shellSnapshot;
    void buildPageLayout(ImGuiID dockspaceId);
    void dockPanel(const char* key, ImGuiID node) const;
    void openSectionOn(ImGuiID node, const char* windowTitle);
    void applyPendingSectionTabs();
    std::vector<std::pair<ImGuiID, ImGuiID>> m_pendingSectionTabs; // node, tab
    const char* m_focusAfterLayout = nullptr; // window key focused once a page appears
    // Which windows each tab's page shows, and the tab that shows a window
    // when something asks for it by key.
    static const std::vector<const char*>& pageWindowKeys(workshop::MainTab tab);
    static workshop::MainTab defaultTabForWindow(const std::string& key);

    // Dialogs
    std::unique_ptr<FileDialog> m_fileDialog;
    std::unique_ptr<LightingDialog> m_lightingDialog;
    std::unique_ptr<MachineProfileDialog> m_machineProfileDialog;
    std::unique_ptr<ImportSummaryDialog> m_importSummaryDialog;
    std::unique_ptr<ImportOptionsDialog> m_importOptionsDialog;
    std::unique_ptr<ProgressDialog> m_progressDialog;
    std::unique_ptr<TagImageDialog> m_tagImageDialog;
    std::unique_ptr<MaintenanceDialog> m_maintenanceDialog;
    std::unique_ptr<TaggerShutdownDialog> m_taggerShutdownDialog;
    std::unique_ptr<SettingsImportDialog> m_settingsImportDialog;
    std::unique_ptr<SupplierToolImportDialog> m_supplierToolImportDialog;

    // Widgets
    std::unique_ptr<StatusBar> m_statusBar;

    // Context menu manager
    std::unique_ptr<ContextMenuManager> m_contextMenuManager;

    // Restart popup state
    bool m_showRestartPopup = false;
    bool m_showResetToDefaultsPopup = false;
    bool m_openAboutRequested = false;
    std::string m_resetToDefaultsError;

    bool m_focusToolLibraryNextFrame = false;

    // Top bar helpers (ui_manager_top_bar.cpp)
    void renderAppMenuButton();
    void renderTabButtons(bool iconsOnly);
    void renderMachineStatus();
    void renderPrimaryActions();
    void sendToMachine();

    // Menu helpers (ui_manager_menus.cpp)
    void renderFileMenu();
    void renderViewMenu();
    void renderLiveOverlayMenu();
    void renderEditMenu();
    void renderToolsMenu();
    void renderHelpMenu();
    bool checkPanicStop();

    // Action callbacks (delegated to Application)
    ActionCallback m_onNewProject;
    ActionCallback m_onOpenProject;
    ActionCallback m_onSaveProject;
    ActionCallback m_onImportModel;
    ActionCallback m_onImportFolder;
    ActionCallback m_onExportModel;
    ActionCallback m_onImportProjectArchive;
    ActionCallback m_onQuit;
    ActionCallback m_onSpawnSettings;
    ActionCallback m_onShowAbout;
    ActionCallback m_onLibraryMaintenance;
    ActionCallback m_onStartBackgroundTagging;
    ActionCallback m_onStopBackgroundTagging;
    ActionCallback m_onRelocateWorkspace;
    ActionCallback m_onLocateMissingFiles;
    ActionCallback m_onExportSettings;
    ActionCallback m_onImportSettings;
    ResetToDefaultsCallback m_onResetToDefaults;
    std::vector<ActionCallback> m_onMachineProfileChanged;

    // CNC menu bar callbacks
    ConnectCallback m_onConnect;
    ActionCallback m_onDisconnect;
    ActionCallback m_onPanicStop;

    // CNC state for menu bar display
    bool m_cncConnected = false;
    bool m_cncSimulating = false;
    bool m_cncStreaming = false;
    std::vector<std::string> m_availablePorts;

    // Keyboard smash panic detector
    static constexpr int PANIC_KEY_COUNT = 5;       // keys required
    static constexpr float PANIC_WINDOW_SEC = 0.4f; // time window
    float m_panicKeyTimes[PANIC_KEY_COUNT] = {};
    int m_panicKeyHead = 0;
};

} // namespace dw
