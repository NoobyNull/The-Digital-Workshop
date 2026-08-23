// Project-focused FileIOManager operations. Kept separate from model import and
// thumbnail processing so project lifecycle policy can remain behind one port.

#include "managers/file_io_manager.h"

#include <filesystem>
#include <thread>

#include "core/config/config.h"
#include "core/export/project_export_manager.h"
#include "core/project/named_project_creation.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/project/project_directory_importer.h"
#include "core/paths/path_resolver.h"
#include "core/threading/main_thread_queue.h"
#include "core/utils/file_utils.h"
#include "core/utils/log.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/message_dialog.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

Path normalizedProjectPath(const Path& path) {
    std::error_code error;
    const Path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

bool sameProjectPath(const Path& lhs, const Path& rhs) {
    return normalizedProjectPath(lhs) == normalizedProjectPath(rhs);
}

const char* preparationFailureMessage(NamedProjectPrepareStatus status) {
    switch (status) {
    case NamedProjectPrepareStatus::StorageFailed:
        return "The project folder could not be prepared.";
    case NamedProjectPrepareStatus::CleanupFailed:
        return "The project could not be prepared, and its empty database record could not be cleaned up safely.";
    case NamedProjectPrepareStatus::CreateFailed:
        return "The project could not be created.";
    case NamedProjectPrepareStatus::Prepared:
        break;
    }
    return "The project could not be created.";
}

} // namespace

void FileIOManager::activateProject(std::shared_ptr<Project> project,
                                    std::optional<uint64_t> expectedGeneration,
                                    ProjectActivationCompletion completion) {
    if (!project) {
        if (completion)
            completion(false);
        return;
    }
    if (!m_projectActivationCallback) {
        log::error("FileIO", "Project activation gateway is not configured");
        if (completion)
            completion(false);
        return;
    }
    m_projectActivationCallback(std::move(project), expectedGeneration, std::move(completion));
}

std::optional<uint64_t> FileIOManager::captureProjectGeneration() const {
    if (!m_projectGenerationCallback)
        return std::nullopt;
    return m_projectGenerationCallback();
}

void FileIOManager::newProject(std::string name, ProjectActivationCompletion completion) {
    const auto expectedGeneration = captureProjectGeneration();
    NamedProjectCreationService creation(*m_projectManager);
    auto prepared = creation.prepare(std::move(name));
    if (!prepared.prepared()) {
        ToastManager::instance().show(ToastType::Error,
                                      "Project Creation Failed",
                                      preparationFailureMessage(prepared.status));
        if (completion)
            completion(false);
        return;
    }
    const NamedProjectCreationToken token = prepared.token;
    activateProject(std::move(prepared.project),
                    expectedGeneration,
                    [this,
                     token,
                     completion = std::move(completion)](bool activated) {
                        NamedProjectCreationService finisher(*m_projectManager);
                        const auto status = finisher.finish(token, activated);
                        bool creationSucceeded = false;
                        switch (status) {
                        case NamedProjectFinishStatus::Published:
                            creationSucceeded = true;
                            break;
                        case NamedProjectFinishStatus::NeedsSaving:
                            creationSucceeded = true;
                            ToastManager::instance().show(
                                ToastType::Warning,
                                "Project Needs Saving",
                                "The project is open, but its permanent folder could not be published. Use Save to try again.");
                            break;
                        case NamedProjectFinishStatus::RejectedCleaned:
                            break;
                        case NamedProjectFinishStatus::CleanupFailed:
                            ToastManager::instance().show(
                                ToastType::Error,
                                "Project Cleanup Failed",
                                "The unused temporary project was preserved because its ownership could not be verified safely.");
                            break;
                        case NamedProjectFinishStatus::ActiveIdentityMismatch:
                            ToastManager::instance().show(
                                ToastType::Error,
                                "Project Activation Changed",
                                "A different project became active. The new project candidate was preserved without publishing it.");
                            break;
                        case NamedProjectFinishStatus::InvalidToken:
                            ToastManager::instance().show(
                                ToastType::Error,
                                "Project Creation Failed",
                                "The project creation identity was lost before activation completed.");
                            break;
                        }
                        if (completion)
                            completion(creationSucceeded);
                    });
}

void FileIOManager::openProject(ProjectActivationCompletion completion) {
    if (!m_fileDialog) {
        if (completion)
            completion(false);
        return;
    }

    const auto expectedGeneration = captureProjectGeneration();
    m_fileDialog->showNativeFolder(
        "Open Project", [this, completion, expectedGeneration](const std::string& pathText) {
            if (pathText.empty()) {
                if (completion)
                    completion(false);
                return;
            }
            openProjectDirectory(Path(pathText), expectedGeneration, completion, false);
        });
}

void FileIOManager::openProjectDirectory(const Path& path,
                                         std::optional<uint64_t> expectedGeneration,
                                         ProjectActivationCompletion completion,
                                         bool recentEntry) {
    const Path durablePath =
        PathResolver::durableLocation(path, PathCategory::Projects);
    const Path resolvedPath =
        PathResolver::resolve(durablePath, PathCategory::Projects);
    ProjectDirectory directory;
    if (!directory.open(resolvedPath)) {
        if (recentEntry) {
            Config::instance().removeRecentProject(path);
            Config::instance().save();
        }
        MessageDialog::warning("Not a Project",
                               "Choose a Digital Workshop project folder containing project.json.");
        if (completion)
            completion(false);
        return;
    }

    auto finishActivation = [completion](const Path& activatedPath, bool activated) {
        if (activated) {
            Config::instance().addRecentProject(activatedPath);
            Config::instance().save();
        }
        if (completion)
            completion(activated);
    };

    for (const auto& record : m_projectManager->listProjects()) {
        const Path durableRecordPath =
            PathResolver::durableLocation(record.filePath, PathCategory::Projects);
        if (!sameProjectPath(durableRecordPath, durablePath))
            continue;
        const Path resolvedRecordPath =
            PathResolver::resolve(durableRecordPath, PathCategory::Projects);
        auto project = m_projectManager->open(record.id);
        if (!project) {
            MessageDialog::warning("Project Could Not Be Opened",
                                   "The project record could not be loaded.");
            if (completion)
                completion(false);
            return;
        }
        project->setFilePath(resolvedRecordPath);
        const Path activatedPath = project->filePath();
        activateProject(std::move(project),
                        expectedGeneration,
                        [finishActivation = std::move(finishActivation), activatedPath](
                            bool activated) { finishActivation(activatedPath, activated); });
        return;
    }

    if (!m_database || !m_libraryManager) {
        MessageDialog::warning("Project Could Not Be Opened",
                               "Project-folder import services are unavailable.");
        if (completion)
            completion(false);
        return;
    }

    ProjectDirectoryImporter importer(*m_database, *m_libraryManager);
    auto imported = importer.hydrate(directory);
    if (!imported.success()) {
        MessageDialog::warning("Project Could Not Be Opened",
                               imported.message.empty()
                                   ? "The project folder could not be imported safely."
                                   : imported.message);
        if (completion)
            completion(false);
        return;
    }

    auto project = m_projectManager->open(*imported.projectId);
    if (!project) {
        (void)m_projectManager->remove(*imported.projectId);
        MessageDialog::warning("Project Could Not Be Opened",
                               "The imported project could not be loaded.");
        if (completion)
            completion(false);
        return;
    }

    project->setFilePath(resolvedPath);
    const i64 projectId = project->id();
    const Path activatedPath = project->filePath();
    activateProject(std::move(project),
                    expectedGeneration,
                    [this,
                     projectId,
                     activatedPath,
                     finishActivation = std::move(finishActivation)](bool activated) mutable {
                        if (!activated)
                            (void)m_projectManager->remove(projectId);
                        finishActivation(activatedPath, activated);
                    });
}

bool FileIOManager::saveProject() {
    auto project = m_projectManager->currentProject();
    if (!project) {
        MessageDialog::warning("No Project", "No project is currently open.");
        return false;
    }

    const bool promoted = project->isTemporary();
    const bool saved = project->isTemporary() ? m_projectManager->saveTemporaryProject()
                                               : m_projectManager->save(*project);
    if (saved && !project->filePath().empty()) {
        Config::instance().addRecentProject(project->filePath());
        Config::instance().save();
        if (m_projectSavedCallback)
            m_projectSavedCallback();
        ToastManager::instance().show(
            ToastType::Success,
            promoted ? "Project Published" : "Project Saved",
            promoted ? "The project now has a permanent workshop folder."
                     : "Project changes were saved successfully.");
        return true;
    }

    ToastManager::instance().show(
        ToastType::Error,
        "Project Save Failed",
        "Nothing was discarded. The project remains open so you can try again.");
    return false;
}

void FileIOManager::openRecentProject(const Path& path, ProjectActivationCompletion completion) {
    if (!m_projectManager) {
        if (completion)
            completion(false);
        return;
    }
    openProjectDirectory(path, captureProjectGeneration(), std::move(completion), true);
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
        MessageDialog::warning(
            "No Model", "Choose a model for the project before exporting.");
        return;
    }

    std::string defaultName = project->name() + ".dwproj";
    m_fileDialog->showSave(
        "Export Project Archive",
        FileDialog::projectFilters(),
        defaultName,
        [this, project](const std::string& path) {
            if (path.empty())
                return;

            Path outputPath{path};
            if (outputPath.extension() != ".dwproj")
                outputPath += ".dwproj";

            auto* progressDlg = m_progressDialog;
            auto* exportMgr = m_projectExportManager;
            auto* mtq = m_mainThreadQueue;

            if (progressDlg)
                progressDlg->start("Exporting Project...", project->modelCount(), true);

            startBackgroundTask([project, outputPath, progressDlg, exportMgr, mtq]() {
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
            });
        });
}

void FileIOManager::importProjectArchive(ProjectActivationCompletion completion) {
    if (!m_projectExportManager) {
        MessageDialog::warning("Import Unavailable", "Project import is not available.");
        if (completion)
            completion(false);
        return;
    }
    if (!m_fileDialog) {
        if (completion)
            completion(false);
        return;
    }

    const auto expectedGeneration = captureProjectGeneration();
    m_fileDialog->showOpen(
        "Import Project Archive",
        FileDialog::projectFilters(),
        [this, completion, expectedGeneration](const std::string& path) {
            if (path.empty()) {
                if (completion)
                    completion(false);
                return;
            }

            const Path archivePath(path);
            auto* progressDialog = m_progressDialog;
            auto* exportManager = m_projectExportManager;
            auto* mainThreadQueue = m_mainThreadQueue;
            auto* projectManager = m_projectManager;
            auto activationCallback = m_projectActivationCallback;
            if (progressDialog)
                progressDialog->start("Importing Project...", 1, true);

            startBackgroundTask([archivePath,
                                 progressDialog,
                                 exportManager,
                                 mainThreadQueue,
                                 projectManager,
                                 completion,
                                 expectedGeneration,
                                 activationCallback = std::move(activationCallback)]() mutable {
                auto result = exportManager->importProject(
                    archivePath,
                    [progressDialog](int /*current*/, int /*total*/, const std::string& item) {
                        if (progressDialog)
                            progressDialog->advance(item);
                    });

                mainThreadQueue->enqueue(
                    [result,
                     progressDialog,
                     archivePath,
                     projectManager,
                     completion,
                     expectedGeneration,
                     activationCallback = std::move(activationCallback)]() mutable {
                        if (progressDialog)
                            progressDialog->finish();

                        if (!result.success) {
                            ToastManager::instance().show(ToastType::Error,
                                                          "Import Failed",
                                                          result.error);
                            if (completion)
                                completion(false);
                            return;
                        }

                        if (result.importedProjectId && activationCallback) {
                            auto project = projectManager->open(*result.importedProjectId);
                            if (project) {
                                activationCallback(std::move(project),
                                                   expectedGeneration,
                                                   completion);
                            } else if (completion) {
                                completion(false);
                            }
                        } else if (completion) {
                            completion(false);
                        }
                        ToastManager::instance().show(ToastType::Success,
                                                      "Project Imported",
                                                      archivePath.stem().string() + " (" +
                                                          std::to_string(result.modelCount) +
                                                          " models)");
                    });
            });
        });
}

} // namespace dw
