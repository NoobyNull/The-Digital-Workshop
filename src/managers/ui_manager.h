#pragma once

// Digital Workshop - UI Manager
// Owns all UI panels, dialogs, visibility state, menu bar,
// keyboard shortcuts, import progress overlay, about/restart popups,
// and dock layout. Extracted from Application (god class decomposition).
//
// Implementation split across:
//   ui_manager.cpp         — init, shutdown, panel rendering, background UI
//   ui_manager_menus.cpp   — menu bar, keyboard shortcuts, about/restart dialogs
//   ui_manager_layout.cpp  — dock layout, presets, config save/restore

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/config/layout_preset.h"
#include "../core/config/window_catalog.h"
#include "../core/types.h"

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
class CostingPanel;
class CutOptimizerPanel;
class MaterialsPanel;
class StartPage;
class ToolBrowserPanel;
class DirectCarvePanel;
class GroupPanel;

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
class CutPlanRepository;
class CostRepository;
class RateCategoryRepository;
class ImportQueue;
class Workspace;

// Forward declarations - widgets
class StatusBar;
class ContextMenuManager;

// Callback types for actions that remain in Application
// Workspace mode — controls which panels are visible by default
enum class WorkspaceMode { Model, CNC };

using ActionCallback = std::function<void()>;
using ModelIdCallback = std::function<void(int64_t)>;
using PathCallback = std::function<void(const Path&)>;
using PathsCallback = std::function<void(const std::vector<std::string>&)>;

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
              CostRepository* costRepo = nullptr,
              RateCategoryRepository* rateCatRepo = nullptr,
              ModelRepository* modelRepo = nullptr,
              GCodeRepository* gcodeRepo = nullptr,
              CutPlanRepository* cutPlanRepo = nullptr);

    // Shutdown and destroy all UI resources
    void shutdown();

    // --- Per-frame rendering ---
    void renderMenuBar();
    void renderPanels();
    void renderAboutDialog();
    void renderRestartPopup(const ActionCallback& onRelaunch);
    void setupDefaultDockLayout(ImGuiID dockspaceId);
    void handleKeyboardShortcuts();

    // --- Background UI (StatusBar, ToastManager, ImportSummaryDialog) ---
    void renderBackgroundUI(float deltaTime, const LoadingState* loadingState);

    // --- Dock layout first-frame logic ---
    [[nodiscard]] bool isFirstFrame() const { return m_firstFrame; }
    void clearFirstFrame() { m_firstFrame = false; }

    // --- Panel accessors (Application needs these for wiring callbacks) ---
    ViewportPanel* viewportPanel() { return m_viewportPanel.get(); }
    LibraryPanel* libraryPanel() { return m_libraryPanel.get(); }
    PropertiesPanel* propertiesPanel() { return m_propertiesPanel.get(); }
    ProjectPanel* projectPanel() { return m_projectPanel.get(); }
    GCodePanel* gcodePanel() { return m_gcodePanel.get(); }
    CutOptimizerPanel* cutOptimizerPanel() { return m_cutOptimizerPanel.get(); }
    MaterialsPanel* materialsPanel() { return m_materialsPanel.get(); }
    CostingPanel* costPanel() { return m_costPanel.get(); }
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
    DirectCarvePanel* directCarvePanel() { return m_directCarvePanel.get(); }
    FileDialog* fileDialog() { return m_fileDialog.get(); }
    LightingDialog* lightingDialog() { return m_lightingDialog.get(); }
    ImportSummaryDialog* importSummaryDialog() { return m_importSummaryDialog.get(); }
    ImportOptionsDialog* importOptionsDialog() { return m_importOptionsDialog.get(); }
    ProgressDialog* progressDialog() { return m_progressDialog.get(); }
    TagImageDialog* tagImageDialog() { return m_tagImageDialog.get(); }
    MaintenanceDialog* maintenanceDialog() { return m_maintenanceDialog.get(); }
    TaggerShutdownDialog* taggerShutdownDialog() { return m_taggerShutdownDialog.get(); }
    ContextMenuManager* contextMenuManager() { return m_contextMenuManager.get(); }

    // --- Visibility state ---
    bool& showViewport() { return m_showViewport; }
    bool& showLibrary() { return m_showLibrary; }
    bool& showProperties() { return m_showProperties; }
    bool& showProject() { return m_showProject; }
    bool& showGCode() { return m_showGCode; }
    bool& showCutOptimizer() { return m_showCutOptimizer; }
    bool& showProjectCosting() { return m_showProjectCosting; }
    bool& showMaterials() { return m_showMaterials; }
    bool& showToolBrowser() { return m_showToolBrowser; }
    bool& showCncStatus() { return m_showCncStatus; }
    bool& showCncJog() { return m_showCncJog; }
    bool& showCncConsole() { return m_showCncConsole; }
    bool& showCncWcs() { return m_showCncWcs; }
    bool& showCncTool() { return m_showCncTool; }
    bool& showCncJob() { return m_showCncJob; }
    bool& showCncSafety() { return m_showCncSafety; }
    bool& showCncSettings() { return m_showCncSettings; }
    bool& showCncMacros() { return m_showCncMacros; }
    bool& showDirectCarve() { return m_showDirectCarve; }
    bool& showStartPage() { return m_showStartPage; }

    // Workspace mode
    WorkspaceMode workspaceMode() const { return m_workspaceMode; }
    void setWorkspaceMode(WorkspaceMode mode);
    void showCncPanels(bool show);
    void openWindow(const std::string& key);
    void toggleWindow(const std::string& key);
    [[nodiscard]] bool isWindowVisible(const std::string& key) const;
    void openToolLibrary();
    void openMachineProfiles();
    void addMachineProfileChangedCallback(ActionCallback cb);
    bool& showRestartPopup() { return m_showRestartPopup; }

    // Layout presets
    void applyLayoutPreset(int presetIndex);
    LayoutPreset captureCurrentLayout(const std::string& name) const;
    void saveCurrentAsPreset(const std::string& name);
    void deletePreset(int index);
    int activePresetIndex() const { return m_activePresetIndex; }

    // Group panels
    void addGroupPanel();

    // --- Action callbacks (set by Application) ---
    void setOnNewProject(ActionCallback cb) { m_onNewProject = std::move(cb); }
    void setOnOpenProject(ActionCallback cb) { m_onOpenProject = std::move(cb); }
    void setOnSaveProject(ActionCallback cb) { m_onSaveProject = std::move(cb); }
    void setOnImportModel(ActionCallback cb) { m_onImportModel = std::move(cb); }
    void setOnExportModel(ActionCallback cb) { m_onExportModel = std::move(cb); }
    void setOnImportProjectArchive(ActionCallback cb) { m_onImportProjectArchive = std::move(cb); }
    void setOnQuit(ActionCallback cb) { m_onQuit = std::move(cb); }
    void setOnSpawnSettings(ActionCallback cb) { m_onSpawnSettings = std::move(cb); }
    void setOnShowAbout(ActionCallback cb) { m_onShowAbout = std::move(cb); }
    void setOnLibraryMaintenance(ActionCallback cb) { m_onLibraryMaintenance = std::move(cb); }
    void setOnRelocateWorkspace(ActionCallback cb) { m_onRelocateWorkspace = std::move(cb); }
    void setOnLocateMissingFiles(ActionCallback cb) { m_onLocateMissingFiles = std::move(cb); }
    void setOnExportSettings(ActionCallback cb) { m_onExportSettings = std::move(cb); }
    void setOnImportSettings(ActionCallback cb) { m_onImportSettings = std::move(cb); }

    // Settings import dialog
    SettingsImportDialog* settingsImportDialog() const;

    // CNC menu bar callbacks
    using ConnectCallback = std::function<void(const std::string& port)>;
    void setOnConnect(ConnectCallback cb) { m_onConnect = std::move(cb); }
    void setOnDisconnect(ActionCallback cb) { m_onDisconnect = std::move(cb); }
    void setOnPanicStop(ActionCallback cb) { m_onPanicStop = std::move(cb); }

    // CNC streaming state (for panic smash detection)
    void setCncStreaming(bool v);

    // CNC state for menu bar display
    void setCncConnected(bool v) { m_cncConnected = v; }
    void setCncSimulating(bool v) { m_cncSimulating = v; }
    void setAvailablePorts(std::vector<std::string> ports) { m_availablePorts = std::move(ports); }

    // --- Import progress callbacks ---
    void setImportProgress(const ImportProgress* progress);
    void showImportSummary(const ImportBatchSummary& summary);
    void setImportCancelCallback(std::function<void()> callback);

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
    std::unique_ptr<CutOptimizerPanel> m_cutOptimizerPanel;
    std::unique_ptr<MaterialsPanel> m_materialsPanel;
    std::unique_ptr<CostingPanel> m_costPanel;
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
    std::unique_ptr<DirectCarvePanel> m_directCarvePanel;

    // Panel visibility
    bool m_showViewport = true;
    bool m_showLibrary = true;
    bool m_showProperties = true;
    bool m_showProject = true;
    bool m_showGCode = false;
    bool m_showCutOptimizer = false;
    bool m_showProjectCosting = false;
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

    // Group panels (blank dockable containers)
    std::vector<std::unique_ptr<GroupPanel>> m_groupPanels;
    int m_nextGroupId = 1;

    // Panel registry — maps panel keys to visibility, rendering, and config
    struct PanelEntry {
        const char* key;         // Layout preset key (e.g. "cnc_status")
        bool* showFlag;          // &m_showCncStatus
        const char* menuLabel;   // View menu display name
        const char* windowTitle; // ImGui window title for focus detection
        Panel* panel;            // Base pointer for render() dispatch
        bool syncClose;          // Handle X-button close → visibility sync
        WindowRole role;         // Workspace ownership
    };
    std::vector<PanelEntry> m_panelRegistry;
    void buildPanelRegistry();
    void enforceWorkspaceBoundary();
    void syncWorkspaceModeToPanels();
    std::vector<std::string> currentStatusTips() const;
    bool senderSurfaceVisible() const;
    static bool isBuiltInWorkshopPreset(int presetIndex) { return presetIndex == 0; }
    static bool isBuiltInSenderPreset(int presetIndex) { return presetIndex == 1; }

    // Dialog list for batch rendering
    std::vector<Dialog*> m_dialogList;

    // Layout preset state
    int m_activePresetIndex = 0;
    bool m_suppressAutoContext = false;
    bool m_showSavePresetPopup = false;
    char m_presetNameBuf[64] = {};
    void checkAutoContextTrigger(const std::string& focusedPanelKey);
    void renderPresetSelector();
    void renderSavePresetPopup();

    // Workspace mode
    WorkspaceMode m_workspaceMode = WorkspaceMode::Model;

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

    // Widgets
    std::unique_ptr<StatusBar> m_statusBar;

    // Context menu manager
    std::unique_ptr<ContextMenuManager> m_contextMenuManager;

    // Restart popup state
    bool m_showRestartPopup = false;

    // First frame flag for dock layout
    bool m_firstFrame = true;
    bool m_focusToolLibraryNextFrame = false;

    // Toolbar and menu rendering helpers (in ui_manager_menus.cpp)
    void renderToolbar();
    void renderFileMenu();
    void renderViewMenu();
    void renderSenderSubmenu();
    void renderEditMenu();
    void renderToolsMenu();
    void renderHelpMenu();
    void renderCncMenuBarStatus();
    bool checkPanicStop();

    // Action callbacks (delegated to Application)
    ActionCallback m_onNewProject;
    ActionCallback m_onOpenProject;
    ActionCallback m_onSaveProject;
    ActionCallback m_onImportModel;
    ActionCallback m_onExportModel;
    ActionCallback m_onImportProjectArchive;
    ActionCallback m_onQuit;
    ActionCallback m_onSpawnSettings;
    ActionCallback m_onShowAbout;
    ActionCallback m_onLibraryMaintenance;
    ActionCallback m_onRelocateWorkspace;
    ActionCallback m_onLocateMissingFiles;
    ActionCallback m_onExportSettings;
    ActionCallback m_onImportSettings;
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
