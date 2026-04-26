#pragma once

// Digital Workshop - File I/O Manager
// Orchestrates all file I/O operations: import, export, project
// new/open/save, drag-and-drop, completed import processing,
// recent project opening. Extracted from Application (god class
// decomposition Wave 2).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/types.h"

namespace dw {

// Forward declarations
class Database;
class LibraryManager;
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

    // Callback for G-code files — routes to G-code panel instead of import pipeline
    using GCodeCallback = std::function<void(const std::string& path)>;
    void setGCodeCallback(GCodeCallback cb) { m_gcodeCallback = std::move(cb); }

    // Import/Export
    void importModel();
    void importFolder();
    void exportModel();
    void onFilesDropped(const std::vector<std::string>& paths);
    void processCompletedImports(ViewportPanel* viewport,
                                 PropertiesPanel* properties,
                                 LibraryPanel* library,
                                 std::function<void(bool)> setShowStartPage);

    // Project operations
    void newProject(std::function<void(bool)> setShowStartPage);
    void openProject(std::function<void(bool)> setShowStartPage);
    void saveProject();
    void openRecentProject(const Path& path, std::function<void(bool)> setShowStartPage);

    // Project archive export/import (.dwproj)
    void exportProjectArchive();
    void importProjectArchive(std::function<void(bool)> setShowStartPage);

    // Dependency injection
    void setProgressDialog(ProgressDialog* dialog) { m_progressDialog = dialog; }
    void setMainThreadQueue(MainThreadQueue* queue) { m_mainThreadQueue = queue; }

  private:
    // Recursive directory scanning helper
    void collectSupportedFiles(const Path& directory, std::vector<Path>& outPaths);

    Database* m_database;
    LibraryManager* m_libraryManager;
    ProjectManager* m_projectManager;
    ImportQueue* m_importQueue;
    Workspace* m_workspace;
    FileDialog* m_fileDialog;
    ThumbnailGenerator* m_thumbnailGenerator;

    // Pending completions queue for throttled processing (one per frame)
    std::vector<ImportTask> m_pendingCompletions;

    // Optional material-aware thumbnail callback
    ThumbnailCallback m_thumbnailCallback;

    // G-code file callback (routes to G-code panel)
    GCodeCallback m_gcodeCallback;

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
