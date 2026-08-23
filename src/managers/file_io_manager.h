#pragma once

// Digital Workshop - File I/O Manager
// Orchestrates all file I/O operations: import, export, project
// new/open/save, drag-and-drop, completed import processing,
// recent project opening. Extracted from Application (god class
// decomposition Wave 2).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../core/types.h"

namespace dw {

// Forward declarations
class Database;
class LibraryManager;
class Project;
class ProjectDirectory;
class ProjectManager;
class ImportQueue;
struct ImportTask;
class Workspace;
class FileDialog;
class ThumbnailGenerator;
class Mesh;
class ViewportPanel;
class PropertiesPanel;
class LibraryPanel;
class ImportOptionsDialog;
class ProjectExportManager;
class ProgressDialog;
class MainThreadQueue;

enum class ImportedLibraryItemKind {
    Model,
    GCode,
};

struct ImportedLibraryItem {
    ImportedLibraryItemKind kind = ImportedLibraryItemKind::Model;
    int64_t id = 0;

    [[nodiscard]] bool valid() const noexcept { return id > 0; }
};

class FileIOManager {
  public:
    FileIOManager(Database* database,
                  LibraryManager* libraryManager,
                  ProjectManager* projectManager,
                  ImportQueue* importQueue,
                  Workspace* workspace,
                  FileDialog* fileDialog,
                  ThumbnailGenerator* thumbnailGenerator,
                  ProjectExportManager* projectExportManager = nullptr);
    ~FileIOManager();

    // Optional callback for thumbnail generation with material support.
    // Signature: (modelId, mesh) -> bool. When set, replaces default thumbnail generation.
    using ThumbnailCallback = std::function<bool(int64_t modelId, Mesh& mesh)>;
    void setThumbnailCallback(ThumbnailCallback cb) { m_thumbnailCallback = std::move(cb); }
    using ImportPostProcessingCallback = std::function<void()>;
    void setImportPostProcessingCallback(ImportPostProcessingCallback cb) {
        m_importPostProcessingCallback = std::move(cb);
    }

    // Callback for G-code files — routes to G-code panel instead of import pipeline
    using GCodeCallback = std::function<void(const std::string& path)>;
    void setGCodeCallback(GCodeCallback cb) { m_gcodeCallback = std::move(cb); }

    // Project identity is committed by the application-owned ProjectSession
    // gateway. The completion runs after the transition is finally applied or
    // rejected, including any asynchronous save/discard prompt.
    using ProjectActivationCompletion = std::function<void(bool activated)>;
    using ProjectActivationCallback = std::function<void(std::shared_ptr<Project>,
                                                         std::optional<uint64_t> expectedGeneration,
                                                         ProjectActivationCompletion)>;
    using ProjectGenerationCallback = std::function<uint64_t()>;
    void setProjectActivationCallback(ProjectActivationCallback callback) {
        m_projectActivationCallback = std::move(callback);
    }
    void setProjectGenerationCallback(ProjectGenerationCallback callback) {
        m_projectGenerationCallback = std::move(callback);
    }
    using ProjectSavedCallback = std::function<void()>;
    void setProjectSavedCallback(ProjectSavedCallback callback) {
        m_projectSavedCallback = std::move(callback);
    }

    // Import/Export
    void importModel();
    void importFolder();
    void exportModel();
    void onFilesDropped(const std::vector<std::string>& paths);
    using ImportsReadyCallback =
        std::function<void(const std::vector<ImportedLibraryItem>& items)>;
    void processCompletedImports(ViewportPanel* viewport,
                                 PropertiesPanel* properties,
                                 LibraryPanel* library,
                                 ImportsReadyCallback onImportsReady);

    // Project operations
    void newProject(std::string name, ProjectActivationCompletion completion = {});
    void openProject(ProjectActivationCompletion completion = {});
    [[nodiscard]] bool saveProject();
    void openRecentProject(const Path& path, ProjectActivationCompletion completion = {});

    // Project archive export/import (.dwproj)
    void exportProjectArchive();
    void importProjectArchive(ProjectActivationCompletion completion = {});

    // Dependency injection
    void setProgressDialog(ProgressDialog* dialog) { m_progressDialog = dialog; }
    void setMainThreadQueue(MainThreadQueue* queue) { m_mainThreadQueue = queue; }

  private:
    // Recursive directory scanning helper
    void collectSupportedFiles(const Path& directory, std::vector<Path>& outPaths);
    void activateProject(std::shared_ptr<Project> project,
                         std::optional<uint64_t> expectedGeneration,
                         ProjectActivationCompletion completion = {});
    void openProjectDirectory(const Path& path,
                              std::optional<uint64_t> expectedGeneration,
                              ProjectActivationCompletion completion,
                              bool recentEntry);
    [[nodiscard]] std::optional<uint64_t> captureProjectGeneration() const;
    void startBackgroundTask(std::function<void()> task);

    Database* m_database;
    LibraryManager* m_libraryManager;
    ProjectManager* m_projectManager;
    ImportQueue* m_importQueue;
    Workspace* m_workspace;
    FileDialog* m_fileDialog;
    ThumbnailGenerator* m_thumbnailGenerator;

    // Pending completions queue for throttled processing (one per frame)
    std::vector<ImportTask> m_pendingCompletions;
    std::vector<std::thread> m_backgroundThreads;

    // Optional material-aware thumbnail callback
    ThumbnailCallback m_thumbnailCallback;
    ImportPostProcessingCallback m_importPostProcessingCallback;

    // G-code file callback (routes to G-code panel)
    GCodeCallback m_gcodeCallback;
    ProjectActivationCallback m_projectActivationCallback;
    ProjectGenerationCallback m_projectGenerationCallback;
    ProjectSavedCallback m_projectSavedCallback;

    // Import options dialog (owned by UIManager, nullable)
    ImportOptionsDialog* m_importOptionsDialog = nullptr;

    // Project export/import
    ProjectExportManager* m_projectExportManager = nullptr;
    ProgressDialog* m_progressDialog = nullptr;
    MainThreadQueue* m_mainThreadQueue = nullptr;

  public:
    void setImportOptionsDialog(ImportOptionsDialog* dialog) { m_importOptionsDialog = dialog; }
};

} // namespace dw
