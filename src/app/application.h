#pragma once

// Digital Workshop - Application Class
// Main application lifecycle: init, run loop, shutdown.
// UI ownership delegated to UIManager (src/managers/ui_manager.h).
// File I/O orchestration delegated to FileIOManager (src/managers/file_io_manager.h).
// Config management delegated to ConfigManager (src/managers/config_manager.h).

#include <csignal>
#include <functional>
#include <memory>
#include <optional>
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
class Project;
class ProjectDirectory;
class ProjectManager;
class ProjectSessionIntegration;
struct ProjectSessionIntegrationResult;
class ProjectResumeFileStore;
class LibraryWorkflowCoordinator;
struct ProjectOpenItem;
struct ImportedLibraryItem;
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

namespace carve {
class CarveJob;
}

namespace carve_preparation {
class PrepareCarvePin;
}

// Managers (extracted from Application)
class UIManager;
class FileIOManager;
class ConfigManager;
class DirectCarveRunEffectAdapter;
class ProjectPlanRunTruthAdapter;

namespace workshop {
class ProjectSession;
class ProjectWorkshopController;
class ProjectResumeCoordinator;
struct ProjectDisplayFacts;
struct ProjectItemRef;
enum class ExperienceMode;
enum class ProjectClosePurpose;
} // namespace workshop

namespace design_library {
enum class LibraryPickerPurpose;
} // namespace design_library

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

    // The process signal handler may only store to sig_atomic_t. The run loop
    // observes and clears this flag before invoking the ordinary quit path on
    // the main thread.
    void setTerminationSignalFlag(volatile std::sig_atomic_t* flag) noexcept {
        m_terminationSignalFlag = flag;
    }

    // Explicit facilitator-only River Sign setup followed by the ordinary
    // interactive run loop. Normal launches never enter this path.
    int runRiverSignStudy(const Path& fixtureDirectory);

    // Request application to quit
    void quit();

#ifdef DW_ENABLE_UX_CAPTURE
    // Capture-only entry point. This member is absent from normal and packaged
    // binaries because the owning translation units are conditionally built.
    int runUxCapture(const std::string& scenarioName,
                     int holdMilliseconds,
                     const Path& outputPath);
    bool writeUxCaptureBackBuffer(const Path& outputPath,
                                  std::string& error);
#endif

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
    void initWiring();          // Dispatcher — calls all wire* functions below
    void wireWorkshop();        // Project shell, Home, Library, and Project intents
    void wireImportPipeline();  // Import queue, options dialog, re-import
    void wireStartPage();       // Start page buttons
    void wireLibraryPanel();    // Library panel: selection, thumbnails, tagging
    void wireProjectPanel();    // Project panel: navigation, cross-panel links
    void wireCncPanels();       // GCode, CNC status/jog/console, DirectCarve, tools
    void wirePropertiesPanel(); // Properties: mesh, color, grain, material
    void wireMaterialsPanel();  // Materials: assignment, AI generation
    void wireMenuActions();     // File/Tools menu, maintenance, relocator, quit
    void wireTagDialog();       // Tag Image dialog: request, save, batch tagging
    void wireToolsMenu();       // Maintenance, relocator, missing files
    void initializeProjectSession();
    void initializeProjectResume();
    void initializeLibraryWorkflow();
    [[nodiscard]] bool restoreProjectResume();
    void showHome(bool beginNamedProject = false);
    [[nodiscard]] bool showDesignLibrary();
    [[nodiscard]] bool showDesignLibrary(design_library::LibraryPickerPurpose purpose);
    [[nodiscard]] workshop::ExperienceMode libraryExperienceMode() const;
    [[nodiscard]] bool requestLibraryReturn(std::string& errorMessage);
    void handleCompletedLibraryImports(const std::vector<ImportedLibraryItem>& items);
    void refreshProjectShell();
    void initializeFileIOManager();
    void requestProjectActivation(std::shared_ptr<Project> project,
                                  std::optional<uint64_t> expectedGeneration,
                                  std::function<void(bool)> completion = {});
    void requestProjectClose(workshop::ProjectClosePurpose purpose,
                             std::function<void(bool)> completion = {});
    [[nodiscard]] bool beginPrepareCarve(workshop::ProjectItemRef target);
    void requestPinnedProjectDirectory(
        const carve_preparation::PrepareCarvePin& pin,
        std::function<void(std::shared_ptr<ProjectDirectory>)> completion);
    void finishProjectTransition(ProjectSessionIntegrationResult result,
                                 std::function<void(bool)> completion);
    void invalidateProjectFocus();
    enum class ProjectItemActivationStatus { Applied, Pending, Rejected };
    enum class ProjectItemContentStatus { Opened, IdentityOnly, Pending, Unavailable };
    enum class ModelSelectionStatus { Loaded, Failed, Superseded };
    enum class ModelLoadPurpose { ProjectContent, LibraryPreview };
    [[nodiscard]] ProjectItemActivationStatus
    activateProjectOpenItem(const ProjectOpenItem& item, bool notifyFailure = true);
    [[nodiscard]] ProjectItemContentStatus
    openProjectItemContent(const ProjectOpenItem& item,
                           std::function<void(bool)> completion);
    bool completeProjectItemActivation(const workshop::ProjectItemRef& item,
                                       bool opened,
                                       bool notifyFailure);
    void clearProjectResumeItem();
    [[nodiscard]] uint64_t projectSessionGeneration() const;
    [[nodiscard]] std::optional<int64_t> activeProjectIdentity() const;
    [[nodiscard]] bool modelLoadStillCurrent(uint64_t loadGeneration,
                                             std::optional<int64_t> projectIdentity,
                                             int64_t modelId,
                                             ModelLoadPurpose purpose) const;

    // Callbacks (business logic stays in Application)
    void regenerateThumbnails(const std::vector<int64_t>& modelIds);
    void regenerateSingleThumbnail(int64_t modelId);
    void regenerateBatchThumbnails(const std::vector<int64_t>& modelIds);
    void handleTagImage(const std::vector<int64_t>& modelIds);
    void handleRelocateWorkspace();
    void handleLocateMissingFiles();
    std::string handleResetToDefaults();
    bool prepareAiTagging(std::string& endpoint, std::string& model);
    void onModelSelected(
        int64_t modelId,
        std::function<void(ModelSelectionStatus)> completion = {},
        std::function<bool()> resultGuard = {},
        ModelLoadPurpose purpose = ModelLoadPurpose::ProjectContent);
    void applySelectedModelMaterial(int64_t modelId, bool assignFallbackMaterial);
    void assignMaterialToCurrentModel(int64_t materialId);
    void loadMaterialTextureForModel(int64_t modelId);
    bool generateMaterialThumbnail(int64_t modelId, Mesh& mesh);
    bool generateMaterialThumbnail(int64_t modelId, Mesh& mesh, ThumbnailView view);
    bool regenerateSmartTagThumbnail(int64_t modelId, ThumbnailView view);
    bool applyAiOrientationCorrection(int64_t modelId, int clockwiseDegrees);

    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    bool m_running = false;
    volatile std::sig_atomic_t* m_terminationSignalFlag = nullptr;
    bool m_initialized = false;
    bool m_skipWorkspaceSaveOnShutdown = false;
    bool m_temporaryProjectDecisionPending = false;
    int m_deferredLibraryCloseFrames = 0;

    // Core systems
    std::unique_ptr<MainThreadQueue> m_mainThreadQueue;
    std::unique_ptr<Database> m_database;
    std::unique_ptr<ConnectionPool> m_connectionPool;
    std::unique_ptr<LibraryManager> m_libraryManager;
    std::unique_ptr<ProjectManager> m_projectManager;
    std::unique_ptr<workshop::ProjectSession> m_projectSession;
    std::unique_ptr<workshop::ProjectWorkshopController> m_projectWorkshopController;
    std::unique_ptr<ProjectResumeFileStore> m_projectResumeStore;
    std::unique_ptr<workshop::ProjectResumeCoordinator> m_projectResumeCoordinator;
    std::unique_ptr<workshop::ProjectDisplayFacts> m_projectDisplayFacts;
    std::optional<uint64_t> m_projectDisplayGeneration;
    std::unique_ptr<ProjectSessionIntegration> m_projectSessionIntegration;
    std::unique_ptr<LibraryWorkflowCoordinator> m_libraryWorkflow;
    std::optional<design_library::LibraryPickerPurpose> m_pendingImportLibraryPurpose;
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
    std::unique_ptr<DirectCarveRunEffectAdapter> m_directCarveRunEffectAdapter;
    std::unique_ptr<ProjectPlanRunTruthAdapter> m_projectPlanRunTruthAdapter;
    uint64_t m_nextPreparationToken = 1;

    // Currently focused model ID (for material assignment)
    int64_t m_focusedModelId = -1;
    bool m_focusedModelIsLibraryPreview = false;

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

#ifdef DW_ENABLE_UX_CAPTURE
    std::optional<Path> m_pendingUxCaptureOutput;
    std::string m_uxCaptureWriteError;
    bool m_uxCaptureWriteComplete = false;
    bool m_uxCaptureWriteSucceeded = false;
#endif

    static constexpr int DEFAULT_WIDTH = 1280;
    static constexpr int DEFAULT_HEIGHT = 720;
    static constexpr const char* WINDOW_TITLE = "Digital Workshop";
};

} // namespace dw
