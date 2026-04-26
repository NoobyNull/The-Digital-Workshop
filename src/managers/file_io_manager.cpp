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

FileIOManager::~FileIOManager() = default;

void FileIOManager::importModel() {
    if (m_fileDialog) {
        m_fileDialog->showNativeOpenMulti("Import Models",
                                          FileDialog::modelFilters(),
                                          [this](const std::vector<std::string>& paths) {
                                        if (paths.empty())
                                            return;

                                        std::vector<Path> importPaths;
                                        for (const auto& p : paths) {
                                            Path path{p};
                                            auto collected =
                                                import_paths::collectSupportedModelFiles(path);
                                            importPaths.insert(importPaths.end(),
                                                               collected.begin(),
                                                               collected.end());
                                        }

                                        if (!importPaths.empty()) {
                                            if (m_importOptionsDialog) {
                                                m_importOptionsDialog->open(importPaths);
                                            } else if (m_importQueue) {
                                                m_importQueue->enqueue(importPaths);
                                            }
                                        }
                                    });
    }
}

void FileIOManager::importFolder() {
    if (!m_fileDialog)
        return;

    m_fileDialog->showNativeFolder("Import Folder", [this](const std::string& path) {
        if (path.empty())
            return;

        if (!m_mainThreadQueue || !m_progressDialog) {
            auto importPaths = import_paths::collectSupportedModelFiles(Path(path));
            if (importPaths.empty()) {
                MessageDialog::warning("No Models Found",
                                       "No supported model files were found in that folder.");
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

        progressDialog->start("Scanning Import Folder", 0, true);
        progressDialog->setStatus(folderPath.string());

        std::thread([folderPath,
                     mainThreadQueue,
                     progressDialog,
                     importOptionsDialog,
                     importQueue]() {
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
                                      importPaths = std::move(importPaths)]() mutable {
                progressDialog->finish();

                if (cancelled) {
                    ToastManager::instance().show(ToastType::Info,
                                                  "Folder Import Cancelled",
                                                  "Stopped scanning before import.");
                    return;
                }

                if (importPaths.empty()) {
                    MessageDialog::warning(
                        "No Models Found",
                        "No supported model files were found in that folder.");
                    return;
                }

                if (importOptionsDialog) {
                    importOptionsDialog->open(importPaths);
                } else if (importQueue) {
                    importQueue->enqueue(importPaths);
                }
            });
        }).detach();
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

                std::thread([archivePath, progressDlg, exportMgr, projMgr, mtq]() {
                    if (progressDlg)
                        progressDlg->start("Importing Project...", 1, true);

                    auto result = exportMgr->importProject(
                        archivePath,
                        [progressDlg](int /*current*/, int /*total*/, const std::string& item) {
                            if (progressDlg)
                                progressDlg->advance(item);
                        });

                    mtq->enqueue([result, progressDlg, projMgr, archivePath]() {
                        if (progressDlg)
                            progressDlg->finish();

                        if (result.success) {
                            // Auto-open the imported project
                            if (result.importedProjectId) {
                                auto project = projMgr->open(*result.importedProjectId);
                                if (project) {
                                    projMgr->setCurrentProject(project);
                                }
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
                }).detach();
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
                                            std::function<void(bool)> setShowStartPage) {
    // viewport param kept for API compatibility; focus does not change on import (user decision)
    (void)viewport;

    if (!m_importQueue)
        return;

    // Poll for newly completed tasks and add to pending queue
    auto newlyCompleted = m_importQueue->pollCompleted();
    if (!newlyCompleted.empty()) {
        setShowStartPage(false);
        m_pendingCompletions.insert(m_pendingCompletions.end(),
                                    std::make_move_iterator(newlyCompleted.begin()),
                                    std::make_move_iterator(newlyCompleted.end()));
    }

    // Process at most ONE task per frame to avoid blocking the UI
    if (m_pendingCompletions.empty())
        return;

    auto task = std::move(m_pendingCompletions.front());
    m_pendingCompletions.erase(m_pendingCompletions.begin());

    // Generate thumbnail on main thread (needs GL context)
    if (task.mesh && m_libraryManager) {
        bool thumbnailOk = false;
        if (m_thumbnailCallback) {
            thumbnailOk = m_thumbnailCallback(task.modelId, *task.mesh);
        } else if (m_thumbnailGenerator) {
            m_libraryManager->setThumbnailGenerator(m_thumbnailGenerator);
            thumbnailOk = m_libraryManager->generateThumbnail(task.modelId, *task.mesh);
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

    // Set mesh on workspace for properties display (lightweight, not viewport render)
    if (task.mesh) {
        m_workspace->setFocusedMesh(task.mesh);
        if (properties) {
            properties->setMesh(task.mesh, task.record.name);
        }
    }
}

void FileIOManager::newProject(std::function<void(bool)> setShowStartPage) {
    auto current = m_projectManager->currentProject();
    if (current && current->isModified()) {
        MessageDialog::question(
            "Unsaved Changes",
            "Current project has unsaved changes. Save before creating a new project?",
            [this, setShowStartPage](DialogResult result) {
                if (result == DialogResult::Yes) {
                    saveProject();
                }
                if (result != DialogResult::No && result != DialogResult::Yes) {
                    return; // Dialog closed without answering
                }
                auto project = m_projectManager->create("New Project");
                m_projectManager->setCurrentProject(project);
                setShowStartPage(false);
            });
        return;
    }
    auto project = m_projectManager->create("New Project");
    m_projectManager->setCurrentProject(project);
    setShowStartPage(false);
}

void FileIOManager::openProject(std::function<void(bool)> setShowStartPage) {
    if (!m_fileDialog)
        return;

    auto current = m_projectManager->currentProject();
    if (current && current->isModified()) {
        MessageDialog::question(
            "Unsaved Changes",
            "Current project has unsaved changes. Save before opening another project?",
            [this, setShowStartPage](DialogResult result) {
                if (result == DialogResult::Yes) {
                    saveProject();
                }
                if (result != DialogResult::No && result != DialogResult::Yes) {
                    return;
                }
                openProject(setShowStartPage);
            });
        return;
    }

    m_fileDialog->showOpen("Open Project",
                           FileDialog::projectFilters(),
                           [this, setShowStartPage](const std::string& path) {
                               if (path.empty())
                                   return;

                               // Search existing projects for one matching this file path
                               auto projects = m_projectManager->listProjects();
                               for (const auto& record : projects) {
                                   if (record.filePath == Path(path)) {
                                       auto project = m_projectManager->open(record.id);
                                       if (project) {
                                           m_projectManager->setCurrentProject(project);
                                           Config::instance().addRecentProject(Path(path));
                                           Config::instance().save();
                                           setShowStartPage(false);
                                           return;
                                       }
                                   }
                               }

                               // No existing project found at that path - create a new one
                               // and associate the file path
                               Path filePath(path);
                               std::string name = filePath.stem().string();
                               auto project = m_projectManager->create(name);
                               if (project) {
                                   project->setFilePath(filePath);
                                   m_projectManager->setCurrentProject(project);
                                   Config::instance().addRecentProject(filePath);
                                   Config::instance().save();
                                   setShowStartPage(false);
                               }
                           });
}

void FileIOManager::saveProject() {
    auto project = m_projectManager->currentProject();
    if (!project) {
        MessageDialog::warning("No Project", "No project is currently open.");
        return;
    }

    if (m_projectManager->save(*project) && !project->filePath().empty()) {
        Config::instance().addRecentProject(project->filePath());
        Config::instance().save();
    }
}

void FileIOManager::openRecentProject(const Path& path,
                                      std::function<void(bool)> setShowStartPage) {
    if (!m_projectManager)
        return;

    auto projects = m_projectManager->listProjects();
    for (const auto& record : projects) {
        if (record.filePath == path) {
            auto project = m_projectManager->open(record.id);
            if (project) {
                m_projectManager->setCurrentProject(project);
                Config::instance().addRecentProject(path);
                Config::instance().save();
                setShowStartPage(false);
                return;
            }
        }
    }

    // Project not found in DB -- create new from path
    std::string name = path.stem().string();
    auto project = m_projectManager->create(name);
    if (project) {
        project->setFilePath(path);
        m_projectManager->setCurrentProject(project);
        Config::instance().addRecentProject(path);
        Config::instance().save();
        setShowStartPage(false);
    }
}

void FileIOManager::exportProjectArchive() {
    if (!m_projectExportManager) {
        MessageDialog::warning("Export Unavailable", "Project export is not available.");
        return;
    }

    auto project = m_projectManager->currentProject();
    if (!project) {
        MessageDialog::warning("No Project", "No project is currently open.");
        return;
    }

    if (project->modelIds().empty()) {
        MessageDialog::warning("No Models", "Add models to the project before exporting.");
        return;
    }

    if (!m_fileDialog)
        return;

    std::string defaultName = project->name() + ".dwproj";

    m_fileDialog->showSave(
        "Export Project Archive",
        FileDialog::projectFilters(),
        defaultName,
        [this, project](const std::string& path) {
            if (path.empty())
                return;

            Path outputPath{path};

            // Ensure .dwproj extension
            if (outputPath.extension() != ".dwproj") {
                outputPath += ".dwproj";
            }

            auto* progressDlg = m_progressDialog;
            auto* exportMgr = m_projectExportManager;
            auto* mtq = m_mainThreadQueue;
            int modelCount = project->modelCount();

            if (progressDlg)
                progressDlg->start("Exporting Project...", modelCount, true);

            std::thread([project, outputPath, progressDlg, exportMgr, mtq]() {
                auto result = exportMgr->exportProject(
                    *project,
                    outputPath,
                    [progressDlg](int /*current*/, int /*total*/, const std::string& item) {
                        if (progressDlg)
                            progressDlg->advance(item);
                    });

                mtq->enqueue([result, progressDlg, outputPath]() {
                    if (progressDlg)
                        progressDlg->finish();

                    if (result.success) {
                        ToastManager::instance().show(ToastType::Success,
                                                      "Project Exported",
                                                      outputPath.filename().string() + " (" +
                                                          std::to_string(result.modelCount) +
                                                          " models)");
                    } else {
                        ToastManager::instance().show(ToastType::Error,
                                                      "Export Failed",
                                                      result.error);
                    }
                });
            }).detach();
        });
}

void FileIOManager::importProjectArchive(std::function<void(bool)> setShowStartPage) {
    if (!m_projectExportManager) {
        MessageDialog::warning("Import Unavailable", "Project import is not available.");
        return;
    }

    if (!m_fileDialog)
        return;

    m_fileDialog->showOpen(
        "Import Project Archive",
        FileDialog::projectFilters(),
        [this, setShowStartPage](const std::string& path) {
            if (path.empty())
                return;

            Path archivePath{path};
            auto* progressDlg = m_progressDialog;
            auto* exportMgr = m_projectExportManager;
            auto* projMgr = m_projectManager;
            auto* mtq = m_mainThreadQueue;

            if (progressDlg)
                progressDlg->start("Importing Project...", 1, true);

            std::thread([archivePath, progressDlg, exportMgr, projMgr, mtq, setShowStartPage]() {
                auto result = exportMgr->importProject(
                    archivePath,
                    [progressDlg](int /*current*/, int /*total*/, const std::string& item) {
                        if (progressDlg)
                            progressDlg->advance(item);
                    });

                mtq->enqueue([result, progressDlg, projMgr, archivePath, setShowStartPage]() {
                    if (progressDlg)
                        progressDlg->finish();

                    if (result.success) {
                        setShowStartPage(false);

                        // Auto-open the imported project
                        if (result.importedProjectId) {
                            auto project = projMgr->open(*result.importedProjectId);
                            if (project) {
                                projMgr->setCurrentProject(project);
                            }
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
            }).detach();
        });
}

} // namespace dw
