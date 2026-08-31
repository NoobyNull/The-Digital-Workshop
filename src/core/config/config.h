#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../gcode/machine_profile.h"
#include "../types.h"
#include "input_binding.h"
#include "layout_migration.h"
#include "layout_preset.h"

namespace dw {

// Forward declaration for ParallelismTier from thread_pool.h
enum class ParallelismTier;

// Navigation style for 3D viewport
enum class NavStyle : int {
    Default = 0, // Left=Orbit, Shift+Left=Pan, Middle=Pan, Right=Zoom
    CAD = 1,     // Middle=Orbit, Shift+Middle=Pan, Right=Pan, Scroll=Zoom
    Maya = 2,    // Alt+Left=Orbit, Alt+Middle=Pan, Alt+Right=Zoom
};

NavStyle nextNavStyle(NavStyle style);
const char* navStyleLetter(NavStyle style);
const char* navStyleName(NavStyle style);
const char* navStyleControlHint(NavStyle style);

// File handling mode for imported models
enum class FileHandlingMode : int {
    LeaveInPlace = 0,  // Leave files at original location (no copy)
    CopyToLibrary = 1, // Copy imported files to library directory
    MoveToLibrary = 2, // Move imported files to library directory (delete original)
};

// Application configuration (persisted to config directory)
class Config {
  public:
    // Singleton access
    static Config& instance();

    // Load/save configuration
    bool load();
    bool save();

    // Recent projects
    const std::vector<Path>& getRecentProjects() const { return m_recentProjects; }
    void addRecentProject(const Path& path);
    void removeRecentProject(const Path& path);
    void clearRecentProjects();

    // Config file path (for external watcher)
    Path configFilePath() const { return getConfigFilePath(); }

    // Theme (0=Dark, 1=Light, 2=HighContrast)
    int getThemeIndex() const { return m_themeIndex; }
    void setThemeIndex(int index) { m_themeIndex = index; }

    f32 getUiScale() const { return m_uiScale; }
    void setUiScale(f32 scale) { m_uiScale = scale; }

    bool getShowGrid() const { return m_showGrid; }
    void setShowGrid(bool show) { m_showGrid = show; }

    bool getShowAxis() const { return m_showAxis; }
    void setShowAxis(bool show) { m_showAxis = show; }

    bool getAutoOrient() const { return m_autoOrient; }
    void setAutoOrient(bool v) { m_autoOrient = v; }

    bool getInvertOrbitX() const { return m_invertOrbitX; }
    void setInvertOrbitX(bool v) { m_invertOrbitX = v; }

    bool getInvertOrbitY() const { return m_invertOrbitY; }
    void setInvertOrbitY(bool v) { m_invertOrbitY = v; }

    NavStyle getNavStyle() const { return m_navStyle; }
    void setNavStyle(NavStyle style) { m_navStyle = style; }
    int getNavStyleIndex() const { return static_cast<int>(m_navStyle); }
    void setNavStyleIndex(int index) { m_navStyle = static_cast<NavStyle>(index); }

    // Default paths
    const Path& getLastImportDir() const { return m_lastImportDir; }
    void setLastImportDir(const Path& path) { m_lastImportDir = path; }

    const Path& getLastExportDir() const { return m_lastExportDir; }
    void setLastExportDir(const Path& path) { m_lastExportDir = path; }

    // Window state
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }
    void setWindowSize(int width, int height) {
        m_windowWidth = width;
        m_windowHeight = height;
    }

    bool getWindowMaximized() const { return m_windowMaximized; }
    void setWindowMaximized(bool maximized) { m_windowMaximized = maximized; }

    // Render settings (persisted)
    Vec3 getRenderLightDir() const { return m_lightDir; }
    void setRenderLightDir(const Vec3& dir) { m_lightDir = dir; }

    Vec3 getRenderLightColor() const { return m_lightColor; }
    void setRenderLightColor(const Vec3& c) { m_lightColor = c; }

    Vec3 getRenderAmbient() const { return m_ambient; }
    void setRenderAmbient(const Vec3& a) { m_ambient = a; }

    Color getRenderObjectColor() const { return m_objectColor; }
    void setRenderObjectColor(const Color& c) { m_objectColor = c; }

    f32 getRenderShininess() const { return m_shininess; }
    void setRenderShininess(f32 s) { m_shininess = s; }

    // Input bindings
    InputBinding getBinding(BindAction action) const;
    void setBinding(BindAction action, const InputBinding& binding);

    // Log level (maps to log::Level enum)
    int getLogLevel() const { return m_logLevel; }
    void setLogLevel(int level) { m_logLevel = level; }

    // Log to file (EXT-05)
    bool getLogToFile() const { return m_logToFile; }
    void setLogToFile(bool v) { m_logToFile = v; }

    const Path& getLogFilePath() const { return m_logFilePath; }
    void setLogFilePath(const Path& p) { m_logFilePath = p; }

    // Workspace state (panel visibility)
    bool getShowViewport() const { return m_wsShowViewport; }
    void setShowViewport(bool v) { m_wsShowViewport = v; }

    bool getShowLibrary() const { return m_wsShowLibrary; }
    void setShowLibrary(bool v) { m_wsShowLibrary = v; }

    bool getShowProperties() const { return m_wsShowProperties; }
    void setShowProperties(bool v) { m_wsShowProperties = v; }

    bool getShowProject() const { return m_wsShowProject; }
    void setShowProject(bool v) { m_wsShowProject = v; }

    bool getShowGCode() const { return m_wsShowGCode; }
    void setShowGCode(bool v) { m_wsShowGCode = v; }

    bool getShowMaterials() const { return m_wsShowMaterials; }
    void setShowMaterials(bool v) { m_wsShowMaterials = v; }

    bool getShowCutOptimizer() const { return m_wsShowCutOptimizer; }
    void setShowCutOptimizer(bool v) { m_wsShowCutOptimizer = v; }

    bool getShowProjectCosting() const { return m_wsShowProjectCosting; }
    void setShowProjectCosting(bool v) { m_wsShowProjectCosting = v; }
    // Backward compat alias (reads old config key)
    bool getShowCostEstimator() const { return m_wsShowProjectCosting; }
    void setShowCostEstimator(bool v) { m_wsShowProjectCosting = v; }

    bool getShowToolBrowser() const { return m_wsShowToolBrowser; }
    void setShowToolBrowser(bool v) { m_wsShowToolBrowser = v; }

    // CNC panel visibility
    bool getShowCncStatus() const { return m_wsShowCncStatus; }
    void setShowCncStatus(bool v) { m_wsShowCncStatus = v; }
    bool getShowCncJog() const { return m_wsShowCncJog; }
    void setShowCncJog(bool v) { m_wsShowCncJog = v; }
    bool getShowCncConsole() const { return m_wsShowCncConsole; }
    void setShowCncConsole(bool v) { m_wsShowCncConsole = v; }
    bool getShowCncWcs() const { return m_wsShowCncWcs; }
    void setShowCncWcs(bool v) { m_wsShowCncWcs = v; }

    // WCS aliases (G54-G59 user-defined names)
    static constexpr int NUM_WCS = 6;
    const std::string& getWcsAlias(int idx) const { return m_wcsAliases[idx]; }
    void setWcsAlias(int idx, const std::string& alias) { if (idx >= 0 && idx < NUM_WCS) m_wcsAliases[idx] = alias; }
    bool getShowCncTool() const { return m_wsShowCncTool; }
    void setShowCncTool(bool v) { m_wsShowCncTool = v; }
    bool getShowCncJob() const { return m_wsShowCncJob; }
    void setShowCncJob(bool v) { m_wsShowCncJob = v; }
    bool getShowCncSafety() const { return m_wsShowCncSafety; }
    void setShowCncSafety(bool v) { m_wsShowCncSafety = v; }
    bool getShowCncSettings() const { return m_wsShowCncSettings; }
    void setShowCncSettings(bool v) { m_wsShowCncSettings = v; }
    bool getShowCncMacros() const { return m_wsShowCncMacros; }
    void setShowCncMacros(bool v) { m_wsShowCncMacros = v; }
    bool getShowDirectCarve() const { return m_wsShowDirectCarve; }
    void setShowDirectCarve(bool v) { m_wsShowDirectCarve = v; }

    bool getShowStartPage() const { return m_wsShowStartPage; }
    void setShowStartPage(bool v) { m_wsShowStartPage = v; }

    i64 getLastSelectedModelId() const { return m_wsLastSelectedModelId; }
    void setLastSelectedModelId(i64 id) { m_wsLastSelectedModelId = id; }

    f32 getLibraryThumbSize() const { return m_wsLibraryThumbSize; }
    void setLibraryThumbSize(f32 size) { m_wsLibraryThumbSize = size; }

    f32 getMaterialsThumbSize() const { return m_wsMaterialsThumbSize; }
    void setMaterialsThumbSize(f32 size) { m_wsMaterialsThumbSize = size; }

    // Import pipeline settings
    ParallelismTier getParallelismTier() const { return m_parallelismTier; }
    void setParallelismTier(ParallelismTier tier) { m_parallelismTier = tier; }

    FileHandlingMode getFileHandlingMode() const { return m_fileHandlingMode; }
    void setFileHandlingMode(FileHandlingMode mode) { m_fileHandlingMode = mode; }

    Path getLibraryDir() const { return m_libraryDir; }
    void setLibraryDir(const Path& path) { m_libraryDir = path; }

    // Workspace root (empty = use app_paths::getUserRoot())
    const Path& getWorkspaceRoot() const { return m_workspaceRoot; }
    void setWorkspaceRoot(const Path& path) { m_workspaceRoot = path; }
    Path getEffectiveWorkspaceRoot() const;

    // Category directories (empty means use default from workspace root)
    Path getModelsDir() const;
    void setModelsDir(const Path& path) { m_modelsDir = path; }

    Path getProjectsDir() const;
    void setProjectsDir(const Path& path) { m_projectsDir = path; }

    Path getMaterialsDir() const;
    void setMaterialsDir(const Path& path) { m_materialsDir = path; }

    Path getGCodeDir() const;
    void setGCodeDir(const Path& path) { m_gcodeDir = path; }

    Path getSupportDir() const;
    void setSupportDir(const Path& path) { m_supportDir = path; }

    bool getShowImportErrorToasts() const { return m_showImportErrorToasts; }
    void setShowImportErrorToasts(bool show) { m_showImportErrorToasts = show; }

    bool getEnableFloatingWindows() const { return m_enableFloatingWindows; }
    void setEnableFloatingWindows(bool enable) { m_enableFloatingWindows = enable; }

    // Default material
    i64 getDefaultMaterialId() const { return m_defaultMaterialId; }
    void setDefaultMaterialId(i64 id) { m_defaultMaterialId = id; }

    // Local AI service
    const std::string& getLMStudioEndpoint() const { return m_lmStudioEndpoint; }
    void setLMStudioEndpoint(const std::string& endpoint) { m_lmStudioEndpoint = endpoint; }
    const std::string& getAiProvider() const { return m_aiProvider; }
    void setAiProvider(const std::string& provider) { m_aiProvider = provider; }
    const std::string& getAiModel() const { return m_aiModel; }
    void setAiModel(const std::string& model) { m_aiModel = model; }
    const std::string& getOllamaEndpoint() const { return m_ollamaEndpoint; }
    void setOllamaEndpoint(const std::string& endpoint) { m_ollamaEndpoint = endpoint; }
    int getOllamaPrivatePort() const { return m_ollamaPrivatePort; }
    void setOllamaPrivatePort(int port) { m_ollamaPrivatePort = port; }

    // Display units (true=mm, false=inches) — display-only, commands always use mm
    bool getDisplayUnitsMetric() const { return m_displayUnitsMetric; }
    void setDisplayUnitsMetric(bool v) { m_displayUnitsMetric = v; }

    // Advanced settings view (show raw firmware IDs)
    bool getAdvancedSettingsView() const { return m_advancedSettingsView; }
    void setAdvancedSettingsView(bool v) { m_advancedSettingsView = v; }

    // CNC overlay settings
    bool getCncShowToolDot() const { return m_cncShowToolDot; }
    void setCncShowToolDot(bool v) { m_cncShowToolDot = v; }

    bool getCncShowWorkEnvelope() const { return m_cncShowWorkEnvelope; }
    void setCncShowWorkEnvelope(bool v) { m_cncShowWorkEnvelope = v; }

    bool getCncShowDroOverlay() const { return m_cncShowDroOverlay; }
    void setCncShowDroOverlay(bool v) { m_cncShowDroOverlay = v; }

    f32 getCncToolDotSize() const { return m_cncToolDotSize; }
    void setCncToolDotSize(f32 v) { m_cncToolDotSize = v; }

    Vec4 getCncToolDotColor() const { return m_cncToolDotColor; }
    void setCncToolDotColor(const Vec4& v) { m_cncToolDotColor = v; }

    Vec4 getCncEnvelopeColor() const { return m_cncEnvelopeColor; }
    void setCncEnvelopeColor(const Vec4& v) { m_cncEnvelopeColor = v; }

    // Model bounds overlay (shows model size relative to machine work envelope)
    bool getCncShowModelBounds() const { return m_cncShowModelBounds; }
    void setCncShowModelBounds(bool v) { m_cncShowModelBounds = v; }

    Vec4 getCncModelBoundsInColor() const { return m_cncModelBoundsInColor; }
    void setCncModelBoundsInColor(const Vec4& v) { m_cncModelBoundsInColor = v; }

    Vec4 getCncModelBoundsOutColor() const { return m_cncModelBoundsOutColor; }
    void setCncModelBoundsOutColor(const Vec4& v) { m_cncModelBoundsOutColor = v; }

    // CNC settings
    int getStatusPollIntervalMs() const { return m_statusPollIntervalMs; }
    void setStatusPollIntervalMs(int ms) { m_statusPollIntervalMs = std::clamp(ms, 50, 200); }

    int getJogFeedSmall() const { return m_jogFeedSmall; }
    void setJogFeedSmall(int f) { m_jogFeedSmall = std::clamp(f, 10, 2000); }

    int getJogFeedMedium() const { return m_jogFeedMedium; }
    void setJogFeedMedium(int f) { m_jogFeedMedium = std::clamp(f, 100, 5000); }

    int getJogFeedLarge() const { return m_jogFeedLarge; }
    void setJogFeedLarge(int f) { m_jogFeedLarge = std::clamp(f, 500, 10000); }

    // Safety settings
    bool getSafetyLongPressEnabled() const { return m_safetyLongPressEnabled; }
    void setSafetyLongPressEnabled(bool v) { m_safetyLongPressEnabled = v; }

    int getSafetyLongPressDurationMs() const { return m_safetyLongPressDurationMs; }
    void setSafetyLongPressDurationMs(int v) { m_safetyLongPressDurationMs = v; }

    bool getSafetyAbortLongPress() const { return m_safetyAbortLongPress; }
    void setSafetyAbortLongPress(bool v) { m_safetyAbortLongPress = v; }

    bool getSafetyDeadManEnabled() const { return m_safetyDeadManEnabled; }
    void setSafetyDeadManEnabled(bool v) { m_safetyDeadManEnabled = v; }

    int getSafetyDeadManTimeoutMs() const { return m_safetyDeadManTimeoutMs; }
    void setSafetyDeadManTimeoutMs(int v) { m_safetyDeadManTimeoutMs = v; }

    bool getSafetyDoorInterlockEnabled() const { return m_safetyDoorInterlockEnabled; }
    void setSafetyDoorInterlockEnabled(bool v) { m_safetyDoorInterlockEnabled = v; }

    bool getSafetySoftLimitCheckEnabled() const { return m_safetySoftLimitCheckEnabled; }
    void setSafetySoftLimitCheckEnabled(bool v) { m_safetySoftLimitCheckEnabled = v; }

    bool getSafetyPauseBeforeResetEnabled() const { return m_safetyPauseBeforeResetEnabled; }
    void setSafetyPauseBeforeResetEnabled(bool v) { m_safetyPauseBeforeResetEnabled = v; }

    // Recent G-code files (most recent first, max 10)
    const std::vector<Path>& getRecentGCodeFiles() const { return m_recentGCodeFiles; }
    void addRecentGCodeFile(const Path& path);
    void clearRecentGCodeFiles();

    // Job completion notification settings
    bool getJobCompletionNotify() const { return m_jobCompletionNotify; }
    void setJobCompletionNotify(bool v) { m_jobCompletionNotify = v; }

    bool getJobCompletionFlash() const { return m_jobCompletionFlash; }
    void setJobCompletionFlash(bool v) { m_jobCompletionFlash = v; }

    // Machine profiles
    const std::vector<gcode::MachineProfile>& getMachineProfiles() const { return m_machineProfiles; }
    void setMachineProfiles(const std::vector<gcode::MachineProfile>& profiles) { m_machineProfiles = profiles; }
    int getActiveMachineProfileIndex() const { return m_activeMachineProfileIndex; }
    void setActiveMachineProfileIndex(int index);
    const gcode::MachineProfile& getActiveMachineProfile() const;
    void addMachineProfile(const gcode::MachineProfile& profile);
    void removeMachineProfile(int index);
    void updateMachineProfile(int index, const gcode::MachineProfile& profile);

    // Layout presets
    const std::vector<LayoutPreset>& getLayoutPresets() const { return m_layoutPresets; }
    void setLayoutPresets(const std::vector<LayoutPreset>& presets) { m_layoutPresets = presets; }
    int getActiveLayoutPresetIndex() const { return m_activeLayoutPresetIndex; }
    int getLayoutMigrationVersion() const { return m_layoutMigrationVersion; }
    void setActiveLayoutPresetIndex(int index);
    void addLayoutPreset(const LayoutPreset& preset);
    void removeLayoutPreset(int index);
    void updateLayoutPreset(int index, const LayoutPreset& preset);

  private:
    Config();
    void initDefaultBindings();
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    Path getConfigFilePath() const;

    // Per-section load helpers (called from load())
    void loadUi(const std::string& key, const std::string& value);
    void loadRender(const std::string& key, const std::string& value);
    void loadLogging(const std::string& key, const std::string& value);
    void loadPaths(const std::string& key, const std::string& value);
    void loadWindow(const std::string& key, const std::string& value);
    void loadWorkspace(const std::string& key, const std::string& value);
    void loadBindings(const std::string& key, const std::string& value);
    void loadDirs(const std::string& key, const std::string& value);
    void loadImport(const std::string& key, const std::string& value);
    void loadMaterials(const std::string& key, const std::string& value);
    void loadApi(const std::string& key, const std::string& value);
    void loadRecent(const std::string& key, const std::string& value);
    void loadSafety(const std::string& key, const std::string& value);
    void loadCnc(const std::string& key, const std::string& value);
    void loadWcsAliases(const std::string& key, const std::string& value);
    void loadRecentGCode(const std::string& key, const std::string& value);
    void loadMachineProfiles(const std::string& key, const std::string& value,
                             std::optional<std::vector<gcode::MachineProfile>>& loaded);
    void loadLayoutPresets(const std::string& key, const std::string& value,
                           std::optional<std::vector<LayoutPreset>>& loaded);

    // Post-load resolution for profiles and presets
    void resolveLoadedMachineProfiles(
        std::optional<std::vector<gcode::MachineProfile>>& loaded);
    void resolveLoadedLayoutPresets(
        std::optional<std::vector<LayoutPreset>>& loaded);

    // Per-section save helpers (called from save())
    void saveUi(std::ostringstream& ss) const;
    void saveRender(std::ostringstream& ss) const;
    void saveLogging(std::ostringstream& ss) const;
    void savePaths(std::ostringstream& ss) const;
    void saveWindow(std::ostringstream& ss) const;
    void saveWorkspace(std::ostringstream& ss) const;
    void saveBindings(std::ostringstream& ss) const;
    void saveDirs(std::ostringstream& ss) const;
    void saveImport(std::ostringstream& ss) const;
    void saveMaterials(std::ostringstream& ss) const;
    void saveApi(std::ostringstream& ss) const;
    void saveRecent(std::ostringstream& ss) const;
    void saveCnc(std::ostringstream& ss) const;
    void saveWcsAliases(std::ostringstream& ss) const;
    void saveRecentGCode(std::ostringstream& ss) const;
    void saveSafety(std::ostringstream& ss) const;
    void saveMachineProfiles(std::ostringstream& ss) const;
    void saveLayoutPresets(std::ostringstream& ss) const;

    // Recent projects (most recent first, max 10)
    std::vector<Path> m_recentProjects;
    static constexpr int MAX_RECENT_PROJECTS = 10;

    // UI preferences
    int m_themeIndex = 0; // 0=Dark, 1=Light, 2=HighContrast
    f32 m_uiScale = 1.0f;
    bool m_showGrid = true;
    bool m_showAxis = true;
    bool m_autoOrient = true;
    bool m_invertOrbitX = false;
    bool m_invertOrbitY = false;
    NavStyle m_navStyle = NavStyle::Default;

    // Render settings
    Vec3 m_lightDir{-0.5f, -1.0f, -0.3f};
    Vec3 m_lightColor{1.0f, 1.0f, 1.0f};
    Vec3 m_ambient{0.2f, 0.2f, 0.2f};
    Color m_objectColor{0.4f, 0.6f, 0.8f, 1.0f};
    f32 m_shininess = 32.0f;

    // Logging
    int m_logLevel = 1; // Info
    bool m_logToFile = false;
    Path m_logFilePath; // Empty = default (app data dir / "digital_workshop.log")

    // Default paths
    Path m_lastImportDir;
    Path m_lastExportDir;

    // Window state
    int m_windowWidth = 1600;
    int m_windowHeight = 900;
    bool m_windowMaximized = false;

    // Workspace state
    bool m_wsShowViewport = true;
    bool m_wsShowLibrary = true;
    bool m_wsShowProperties = true;
    bool m_wsShowProject = true;
    bool m_wsShowMaterials = false;
    bool m_wsShowGCode = false;
    bool m_wsShowCutOptimizer = false;
    bool m_wsShowProjectCosting = false;
    bool m_wsShowToolBrowser = false;
    bool m_wsShowCncStatus = false;
    bool m_wsShowCncJog = false;
    bool m_wsShowCncConsole = false;
    bool m_wsShowCncWcs = false;
    std::string m_wcsAliases[6]; // User-defined names for G54-G59
    bool m_wsShowCncTool = false;
    bool m_wsShowCncJob = false;
    bool m_wsShowCncSafety = false;
    bool m_wsShowCncSettings = false;
    bool m_wsShowCncMacros = false;
    bool m_wsShowDirectCarve = false;
    bool m_wsShowStartPage = true;
    i64 m_wsLastSelectedModelId = -1;
    f32 m_wsLibraryThumbSize = 96.0f;
    f32 m_wsMaterialsThumbSize = 96.0f;

    // Input bindings
    std::array<InputBinding, static_cast<int>(BindAction::COUNT)> m_bindings;

    // Import pipeline settings
    ParallelismTier m_parallelismTier;
    FileHandlingMode m_fileHandlingMode = FileHandlingMode::LeaveInPlace;
    Path m_libraryDir; // Empty means default (app data dir / "library")

    // Workspace root (empty = use app_paths::getUserRoot())
    Path m_workspaceRoot;

    // Category directories (empty = default from workspace root)
    Path m_modelsDir;
    Path m_projectsDir;
    Path m_materialsDir;
    Path m_gcodeDir;
    Path m_supportDir;
    bool m_showImportErrorToasts = true;
#ifdef _WIN32
    bool m_enableFloatingWindows = true;
#else
    bool m_enableFloatingWindows = false;
#endif

    // Default material
    i64 m_defaultMaterialId = -1;

    // Local AI service
    std::string m_lmStudioEndpoint = "http://127.0.0.1:1234/v1/chat/completions";
    std::string m_aiProvider = "ollama";
    std::string m_aiModel = "llava:latest";
    std::string m_ollamaEndpoint = "http://127.0.0.1:42319/v1/chat/completions";
    int m_ollamaPrivatePort = 42319;

    // Display units
    bool m_displayUnitsMetric = true;
    bool m_advancedSettingsView = false;

    // CNC overlay settings
    bool m_cncShowToolDot = true;
    bool m_cncShowWorkEnvelope = false;
    bool m_cncShowDroOverlay = true;
    f32 m_cncToolDotSize = 8.0f;
    Vec4 m_cncToolDotColor{1.0f, 0.2f, 0.2f, 1.0f};
    Vec4 m_cncEnvelopeColor{1.0f, 0.67f, 0.2f, 0.6f};
    bool m_cncShowModelBounds = true;
    Vec4 m_cncModelBoundsInColor{0.2f, 1.0f, 0.2f, 0.8f};    // Green for within bounds
    Vec4 m_cncModelBoundsOutColor{1.0f, 0.2f, 0.2f, 0.8f};   // Red for exceeding bounds

    // CNC settings
    int m_statusPollIntervalMs = 200;
    int m_jogFeedSmall = 200;    // mm/min for 0.01-0.1mm steps
    int m_jogFeedMedium = 1000;  // mm/min for 1mm steps
    int m_jogFeedLarge = 3000;   // mm/min for 10-100mm steps

    // Safety settings
    bool m_safetyLongPressEnabled = true;
    int m_safetyLongPressDurationMs = 1000;
    bool m_safetyAbortLongPress = false;
    bool m_safetyDeadManEnabled = true;
    int m_safetyDeadManTimeoutMs = 1000;
    bool m_safetyDoorInterlockEnabled = true;
    bool m_safetySoftLimitCheckEnabled = true;
    bool m_safetyPauseBeforeResetEnabled = true;

    // Recent G-code files (most recent first, max 10)
    std::vector<Path> m_recentGCodeFiles;
    static constexpr int MAX_RECENT_GCODE = 10;

    // Job completion notification
    bool m_jobCompletionNotify = true;
    bool m_jobCompletionFlash = true;

    // Machine profiles
    std::vector<gcode::MachineProfile> m_machineProfiles;
    int m_activeMachineProfileIndex = 0;
    std::string m_activeProfileName; // Temp: used during load to resolve name->index

    // Layout presets
    std::vector<LayoutPreset> m_layoutPresets;
    int m_activeLayoutPresetIndex = 1;
    int m_layoutMigrationVersion = CURRENT_LAYOUT_MIGRATION_VERSION;
};

} // namespace dw
