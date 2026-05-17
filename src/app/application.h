#pragma once

// Digital Workshop - Application Class
// Main application lifecycle: init, run loop, shutdown.
// UI ownership delegated to UIManager (src/managers/ui_manager.h).
// File I/O orchestration delegated to FileIOManager (src/managers/file_io_manager.h).
// Config management delegated to ConfigManager (src/managers/config_manager.h).

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../core/threading/loading_state.h"
#include "../core/types.h"

struct SDL_Window;

namespace dw {

// Forward declarations
class Database;
class ConnectionPool;
class LibraryManager;
class ProjectManager;
class Workspace;
class ThumbnailGenerator;
class ImportQueue;
class MainThreadQueue;
class StorageManager;
class MaterialManager;
class CostRepository;
class RateCategoryRepository;
class GraphManager;
class ModelRepository;
class GCodeRepository;
class CutPlanRepository;
class LMStudioMaterialService;
class LMStudioDescriptorService;
class ProjectExportManager;
class CutListFile;
class CncController;
class JobRepository;
class ToolDatabase;
class ToolboxRepository;
class MacroManager;
class GamepadInput;
class BackgroundTagger;
class ImportLog;
class Mesh;
class Texture;
class OllamaRuntime;
enum class ThumbnailView;

namespace carve { class CarveJob; }

// Managers (extracted from Application)
class UIManager;
class FileIOManager;
class ConfigManager;

} // namespace dw

namespace dw {

class Application {
  public:
    Application();
    ~Application();

    // Disable copy
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Disable move (singleton-like usage)
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Initialize SDL2, OpenGL, ImGui
    bool init();

    // Initialize with diagnostic mode (exits after init)
    bool init(bool diagnosticMode);

    // Main loop - returns exit code
    int run();

    // Request application to quit
    void quit();

    // Accessors
    auto isRunning() const -> bool { return m_running; }
    auto getWindow() const -> SDL_Window* { return m_window; }
    auto mainThreadQueue() -> MainThreadQueue&;

  private:
    void processEvents();
    void update();
    void render();
    void shutdown();
    // Panel/callback wiring — decomposed into domain-specific functions
    void initWiring();           // Dispatcher — calls all wire* functions below
    void wireImportPipeline();   // Import queue, options dialog, re-import
    void wireStartPage();        // Start page buttons
    void wireLibraryPanel();     // Library panel: selection, thumbnails, tagging
    void wireProjectPanel();     // Project panel: navigation, cross-panel links
    void wireCncPanels();        // GCode, CNC status/jog/console, DirectCarve, tools
    void wirePropertiesPanel();  // Properties: mesh, color, grain, material
    void wireMaterialsPanel();   // Materials: assignment, AI generation
    void wireMenuActions();      // File/Tools menu, maintenance, relocator, quit
    void wireTagDialog();        // Tag Image dialog: request, save, batch tagging
    void wireToolsMenu();        // Maintenance, relocator, missing files

    // Callbacks (business logic stays in Application)
    void regenerateThumbnails(const std::vector<int64_t>& modelIds);
    void regenerateSingleThumbnail(int64_t modelId);
    void regenerateBatchThumbnails(const std::vector<int64_t>& modelIds);
    void handleTagImage(const std::vector<int64_t>& modelIds);
    void handleRelocateWorkspace();
    void handleLocateMissingFiles();
    std::string handleResetToDefaults();
    bool prepareAiTagging(std::string& endpoint, std::string& model);
    void onModelSelected(int64_t modelId);
    void assignMaterialToCurrentModel(int64_t materialId);
    void loadMaterialTextureForModel(int64_t modelId);
    bool generateMaterialThumbnail(int64_t modelId, Mesh& mesh);
    bool generateMaterialThumbnail(int64_t modelId, Mesh& mesh, ThumbnailView view);
    bool regenerateSmartTagThumbnail(int64_t modelId, ThumbnailView view);
    bool applyAiOrientationCorrection(int64_t modelId, int clockwiseDegrees);

    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    bool m_running = false;
    bool m_initialized = false;
    bool m_skipWorkspaceSaveOnShutdown = false;

    // Core systems
    std::unique_ptr<MainThreadQueue> m_mainThreadQueue;
    std::unique_ptr<Database> m_database;
    std::unique_ptr<ConnectionPool> m_connectionPool;
    std::unique_ptr<LibraryManager> m_libraryManager;
    std::unique_ptr<ProjectManager> m_projectManager;
    std::unique_ptr<Workspace> m_workspace;
    std::unique_ptr<ThumbnailGenerator> m_thumbnailGenerator;
    std::unique_ptr<ImportQueue> m_importQueue;
    std::unique_ptr<ImportLog> m_importLog;
    std::unique_ptr<BackgroundTagger> m_backgroundTagger;
    bool m_startAiTaggingAfterImportPostProcessing = false;
    std::unique_ptr<StorageManager> m_storageManager;

    // UI Manager - owns all panels, dialogs, visibility state
    std::unique_ptr<UIManager> m_uiManager;

    // File I/O Manager - orchestrates import, export, project operations
    std::unique_ptr<FileIOManager> m_fileIOManager;

    // Config Manager - config watching, applying, workspace state, settings, relaunch
    std::unique_ptr<ConfigManager> m_configManager;

    // Materials Manager - coordinates material archives, defaults, and database
    std::unique_ptr<MaterialManager> m_materialManager;

    // Repositories for project asset navigator
    std::unique_ptr<ModelRepository> m_modelRepo;
    std::unique_ptr<GCodeRepository> m_gcodeRepo;
    std::unique_ptr<CutPlanRepository> m_cutPlanRepo;

    // File-based cut list persistence
    std::unique_ptr<CutListFile> m_cutListFile;

    // Cost estimation repository
    std::unique_ptr<CostRepository> m_costRepo;

    // Rate category repository (consumable/tool cost rates)
    std::unique_ptr<RateCategoryRepository> m_rateCatRepo;

    // Graph query engine (Cypher via GraphQLite extension)
    std::unique_ptr<GraphManager> m_graphManager;

    // LM Studio AI material generation service
    std::unique_ptr<LMStudioMaterialService> m_lmStudioService;

    // LM Studio AI model descriptor (thumbnail classification)
    std::unique_ptr<LMStudioDescriptorService> m_descriptorService;

    // App-owned Ollama runner for private local AI tagging
    std::unique_ptr<OllamaRuntime> m_ollamaRuntime;

    // Project export/import (.dwproj archives)
    std::unique_ptr<ProjectExportManager> m_projectExportManager;

    // CNC tool database (Vectric .vtdb format)
    std::unique_ptr<ToolDatabase> m_toolDatabase;

    // My Toolbox (curated tool subset, stored in main app DB)
    std::unique_ptr<ToolboxRepository> m_toolboxRepo;

    // CNC controller (multi-firmware support: GRBL, grblHAL, FluidNC, Smoothieware)
    std::unique_ptr<CncController> m_cncController;

    // CNC job history (SQLite-backed job recording)
    std::unique_ptr<JobRepository> m_jobRepo;

    // CNC macro manager (SQLite-backed macro storage)
    std::unique_ptr<MacroManager> m_macroManager;

    // CNC gamepad input (SDL_GameController for jog/actions)
    std::unique_ptr<GamepadInput> m_gamepadInput;

    // Direct Carve job (heightmap, analysis, toolpath, streaming)
    std::unique_ptr<carve::CarveJob> m_carveJob;

    // Currently focused model ID (for material assignment)
    int64_t m_focusedModelId = -1;

    // Active material texture for rendering (cached GPU texture)
    std::unique_ptr<Texture> m_activeMaterialTexture;
    int64_t m_activeMaterialId = -1;

    // Model loading state and thread (for async mesh loading)
    LoadingState m_loadingState;
    std::thread m_loadThread;

    // DPI scaling
    void rebuildFontAtlas(float scale);
    float detectDpiScale() const;
    float m_dpiScale = 1.0f;
    float m_uiScale = 1.0f; // Combined dpi * user scale
    int m_displayIndex = 0;

    // Serial port scan timer for CNC auto-connect
    u64 m_lastPortScanMs = 0;
    bool m_wasRealConnection = false;
    std::string m_lastConnectedPort;

    static constexpr int DEFAULT_WIDTH = 1280;
    static constexpr int DEFAULT_HEIGHT = 720;
    static constexpr const char* WINDOW_TITLE = "Digital Workshop";
};

} // namespace dw
