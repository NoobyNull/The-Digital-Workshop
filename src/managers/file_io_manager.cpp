// Digital Workshop - File I/O Manager Implementation
// Extracted from Application: import, export, project operations,
// drag-and-drop, completed import processing, recent project opening.

#include "managers/file_io_manager.h"

#include "app/workspace.h"
#include "core/config/config.h"
#include "core/export/model_exporter.h"
#include "core/import/import_path_collector.h"
#include "core/export/project_export_manager.h"
#include "core/import/import_queue.h"
#include "core/import/import_task.h"
#include "core/import/thumbnail_sidecar.h"
#include "core/library/library_manager.h"
#include "core/loaders/loader_factory.h"
#include "core/project/project.h"
#include "core/utils/file_utils.h"
#include "core/utils/log.h"
#include "core/utils/string_utils.h"
#include "render/thumbnail_generator.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/import_options_dialog.h"
#include "ui/dialogs/message_dialog.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

#include "core/threading/main_thread_queue.h"

#include <thread>

namespace dw {
FileIOManager::FileIOManager(Database* database,
                             LibraryManager* libraryManager,
                             ProjectManager* projectManager,
                             ImportQueue* importQueue,
                             Workspace* workspace,
                             FileDialog* fileDialog,
                             ThumbnailGenerator* thumbnailGenerator,
                             ProjectExportManager* projectExportManager)
    : m_database(database), m_libraryManager(libraryManager), m_projectManager(projectManager),
      m_importQueue(importQueue), m_workspace(workspace), m_fileDialog(fileDialog),
      m_thumbnailGenerator(thumbnailGenerator), m_projectExportManager(projectExportManager) {}

FileIOManager::~FileIOManager() {
    for (auto& thread : m_backgroundThreads) {
        if (thread.joinable())
            thread.join();
    }
}

void FileIOManager::startBackgroundTask(std::function<void()> task) {
    m_backgroundThreads.emplace_back(std::move(task));
}

void FileIOManager::importModel() {
    if (m_fileDialog) {
        m_fileDialog->showNativeOpenMulti("Import Models",
                                          FileDialog::modelFilters(),
                                          [this](const std::vector<std::string>& paths) {
                                        std::vector<Path> importPaths;
                                        for (const auto& p : paths) {
                                            Path path{p};
                                            auto collected =
                                                import_paths::collectSupportedModelFiles(path);
                                            importPaths.insert(importPaths.end(),
                                                               collected.begin(),
                                                               collected.end());
                                        }

                                        if (importPaths.empty()) {
                                            if (m_onImportSelectionAbandoned)
                                                m_onImportSelectionAbandoned();
                                            return;
                                        }
                                        if (m_importOptionsDialog) {
                                            m_importOptionsDialog->open(importPaths);
                                        } else if (m_importQueue) {
                                            m_importQueue->enqueue(importPaths);
                                        }
                                    });
    }
}

void FileIOManager::importFolder() {
    if (!m_fileDialog)
        return;

    m_fileDialog->showNativeFolder("Import Folder", [this](const std::string& path) {
        if (path.empty()) {
            if (m_onImportSelectionAbandoned)
                m_onImportSelectionAbandoned();
            return;
        }

        if (!m_mainThreadQueue || !m_progressDialog) {
            auto importPaths = import_paths::collectSupportedModelFiles(Path(path));
            if (importPaths.empty()) {
                MessageDialog::warning("No Models Found",
                                       "No supported model files were found in that folder.");
                if (m_onImportSelectionAbandoned)
                    m_onImportSelectionAbandoned();
                return;
            }

            if (m_importOptionsDialog) {
                m_importOptionsDialog->open(importPaths);
            } else if (m_importQueue) {
                m_importQueue->enqueue(importPaths);
            }
            return;
        }

        Path folderPath(path);
        auto* mainThreadQueue = m_mainThreadQueue;
        auto* progressDialog = m_progressDialog;
        auto* importOptionsDialog = m_importOptionsDialog;
        auto* importQueue = m_importQueue;
        auto onAbandoned = m_onImportSelectionAbandoned;

        progressDialog->start("Scanning Import Folder", 0, true);
        progressDialog->setStatus(folderPath.string());

        startBackgroundTask([folderPath,
                             mainThreadQueue,
                             progressDialog,
                             importOptionsDialog,
                             importQueue,
                             onAbandoned]() {
            auto importPaths = import_paths::collectSupportedModelFiles(
                folderPath, [progressDialog](const import_paths::ScanProgress& progress) {
                    if (progressDialog->isCancelled()) {
                        return false;
                    }

                    std::string item = progress.currentPath.filename().string();
                    if (item.empty()) {
                        item = progress.currentPath.string();
                    }
                    progressDialog->setStatus(
                        "Scanned " + std::to_string(progress.filesVisited) + " file(s), found " +
                        std::to_string(progress.supportedFilesFound) + " model(s)\n" + item);
                    return true;
                });

            bool cancelled = progressDialog->isCancelled();
            mainThreadQueue->enqueue([progressDialog,
                                      importOptionsDialog,
                                      importQueue,
                                      cancelled,
                                      onAbandoned,
                                      importPaths = std::move(importPaths)]() mutable {
                progressDialog->finish();

                if (cancelled) {
                    ToastManager::instance().show(ToastType::Info,
                                                  "Folder Import Cancelled",
                                                  "Stopped scanning before import.");
                    if (onAbandoned)
                        onAbandoned();
                    return;
                }

                if (importPaths.empty()) {
                    MessageDialog::warning(
                        "No Models Found",
                        "No supported model files were found in that folder.");
                    if (onAbandoned)
                        onAbandoned();
                    return;
                }

                if (importOptionsDialog) {
                    importOptionsDialog->open(importPaths);
                } else if (importQueue) {
                    importQueue->enqueue(importPaths);
                }
            });
        });
    });
}

void FileIOManager::exportModel() {
    auto mesh = m_workspace->getFocusedMesh();
    if (!mesh) {
        MessageDialog::warning("No Model", "No model selected to export.");
        return;
    }

    if (m_fileDialog) {
        m_fileDialog->showSave("Export Model",
                               FileDialog::modelFilters(),
                               "model.stl",
                               [this, mesh](const std::string& path) {
                                   if (path.empty())
                                       return;

                                   ModelExporter exporter;
                                   auto result = exporter.exportMesh(*mesh, path);

                                   if (result.success) {
                                       MessageDialog::info("Export Complete",
                                                           "Model exported to:\n" + path);
                                   } else {
                                       MessageDialog::error("Export Failed", result.error);
                                   }
                               });
    }
}

void FileIOManager::collectSupportedFiles(const Path& directory, std::vector<Path>& outPaths) {
    auto collected = import_paths::collectSupportedModelFiles(directory);
    outPaths.insert(outPaths.end(), collected.begin(), collected.end());
}

void FileIOManager::onFilesDropped(const std::vector<std::string>& paths) {
    if (!m_importQueue)
        return;

    const auto expectedProjectGeneration = captureProjectGeneration();
    std::vector<Path> importPaths;
    int skippedFiles = 0;
    for (const auto& p : paths) {
        Path path{p};

        // Detect .dwproj files and route to project import directly
        if (path.extension() == ".dwproj") {
            if (m_projectExportManager && m_mainThreadQueue) {
                auto archivePath = path;
                auto* progressDlg = m_progressDialog;
                auto* exportMgr = m_projectExportManager;
                auto* projMgr = m_projectManager;
                auto* mtq = m_mainThreadQueue;
                auto activationCallback = m_projectActivationCallback;

                startBackgroundTask(
                    [archivePath, progressDlg, exportMgr, projMgr, mtq,
                     expectedProjectGeneration,
                     activationCallback = std::move(activationCallback)]() mutable {
                    if (progressDlg)
                        progressDlg->start("Importing Project...", 1, true);

                    auto result = exportMgr->importProject(
                        archivePath,
                        [progressDlg](int /*current*/, int /*total*/, const std::string& item) {
                            if (progressDlg)
                                progressDlg->advance(item);
                        });

                    mtq->enqueue([result, progressDlg, projMgr, archivePath,
                                  expectedProjectGeneration,
                                  activationCallback = std::move(activationCallback)]() mutable {
                        if (progressDlg)
                            progressDlg->finish();

                        if (result.success) {
                            // Auto-open the imported project
                            if (result.importedProjectId) {
                                auto project = projMgr->open(*result.importedProjectId);
                                if (project && activationCallback)
                                    activationCallback(std::move(project),
                                                       expectedProjectGeneration,
                                                       {});
                            }

                            ToastManager::instance().show(ToastType::Success,
                                                          "Project Imported",
                                                          archivePath.stem().string() + " (" +
                                                              std::to_string(result.modelCount) +
                                                              " models)");
                        } else {
                            ToastManager::instance().show(ToastType::Error,
                                                          "Import Failed",
                                                          result.error);
                        }
                    });
                    });
            }
            continue;
        }

        // Check if this is a directory
        if (fs::is_directory(path)) {
            collectSupportedFiles(path, importPaths);
        } else {
            // Regular file - check extension
            auto ext = path.extension().string();
            if (!ext.empty() && ext[0] == '.')
                ext = ext.substr(1);

            // Route G-code files directly to G-code panel (not import pipeline)
            std::string lower = str::toLower(ext);
            if ((lower == "gcode" || lower == "nc" || lower == "ngc" || lower == "tap") &&
                m_gcodeCallback) {
                m_gcodeCallback(p);
                continue;
            }

            if (LoaderFactory::isSupported(ext)) {
                importPaths.push_back(path);
            } else {
                ++skippedFiles;
            }
        }
    }

    if (skippedFiles > 0) {
        ToastManager::instance().show(ToastType::Warning,
                                      "Skipped Files",
                                      std::to_string(skippedFiles) +
                                          " unsupported file(s) were skipped");
    }

    if (!importPaths.empty()) {
        if (m_importOptionsDialog) {
            m_importOptionsDialog->open(importPaths);
        } else if (m_importQueue) {
            m_importQueue->enqueue(importPaths);
        }
    }
}

void FileIOManager::processCompletedImports(ViewportPanel* viewport,
                                            PropertiesPanel* properties,
                                            LibraryPanel* library,
                                            ImportsReadyCallback onImportsReady) {
    // viewport param kept for API compatibility; focus does not change on import (user decision)
    (void)viewport;
    (void)properties;

    if (!m_importQueue)
        return;

    // Poll for newly completed tasks and add to pending queue
    auto newlyCompleted = m_importQueue->pollCompleted();
    if (!newlyCompleted.empty()) {
        if (onImportsReady) {
            std::vector<ImportedLibraryItem> importedItems;
            importedItems.reserve(newlyCompleted.size());
            for (const auto& completed : newlyCompleted) {
                const ImportedLibraryItem item{
                    completed.importType == ImportType::Mesh
                        ? ImportedLibraryItemKind::Model
                        : ImportedLibraryItemKind::GCode,
                    completed.importType == ImportType::Mesh ? completed.modelId
                                                             : completed.gcodeId};
                if (item.valid())
                    importedItems.push_back(item);
            }
            if (!importedItems.empty())
                onImportsReady(importedItems);
        }
        m_pendingCompletions.insert(m_pendingCompletions.end(),
                                    std::make_move_iterator(newlyCompleted.begin()),
                                    std::make_move_iterator(newlyCompleted.end()));
    }

    // Process at most ONE task per frame to avoid blocking the UI
    if (m_pendingCompletions.empty())
        return;

    auto task = std::move(m_pendingCompletions.front());
    m_pendingCompletions.erase(m_pendingCompletions.begin());

    // Prefer sidecar image thumbnails; fall back to generated GL thumbnails.
    if (task.importType == ImportType::Mesh && task.mesh && m_libraryManager) {
        bool thumbnailOk = false;
        if (auto sidecar = findSidecarThumbnailForImport(task.sourcePath)) {
            thumbnailOk = m_libraryManager->setThumbnailFromImage(task.modelId, *sidecar);
        }
        if (!thumbnailOk) {
            if (m_thumbnailCallback) {
                thumbnailOk = m_thumbnailCallback(task.modelId, *task.mesh);
            } else if (m_thumbnailGenerator) {
                m_libraryManager->setThumbnailGenerator(m_thumbnailGenerator);
                thumbnailOk = m_libraryManager->generateThumbnail(task.modelId, *task.mesh);
            }
        }
        if (!thumbnailOk) {
            ToastManager::instance().show(ToastType::Warning,
                                          "Thumbnail Failed",
                                          "Could not generate thumbnail for: " + task.record.name);
        }

        // AI categorization is handled by BackgroundTagger (decoupled from import).
        // Models are imported with tag_status=0 (untagged) and the tagger picks them
        // up when started via the import options "Queue for AI tagging" checkbox.
    }

    // Refresh library to show the newly imported model
    if (library) {
        library->refresh();
    }

    if (m_pendingCompletions.empty() && m_importQueue && !m_importQueue->isActive() &&
        m_importPostProcessingCallback) {
        m_importPostProcessingCallback();
    }
}

} // namespace dw
