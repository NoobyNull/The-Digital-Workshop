// Application wiring dispatcher and general-purpose panel/tool callbacks.
// Focused workshop, Library, CNC, AI tagging, and thumbnail wiring live in
// their owned application_wiring_*.cpp units.

#include "app/application.h"

#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "app/workspace.h"
#include "core/config/config.h"
#include "core/config/settings_archive.h"
#include "core/import/background_tagger.h"
#include "core/library/library_manager.h"
#include "core/materials/lmstudio_material_service.h"
#include "core/materials/material_manager.h"
#include "core/paths/app_paths.h"
#include "core/paths/path_recovery.h"
#include "core/threading/main_thread_queue.h"
#include "core/utils/log.h"
#include "core/workspace/workspace_relocator.h"
#include "managers/ui_manager.h"
#include "render/texture.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/maintenance_dialog.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/dialogs/settings_import_dialog.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

void clearDirOverrides(Config& config) {
    config.setModelsDir({});
    config.setProjectsDir({});
    config.setMaterialsDir({});
    config.setGCodeDir({});
    config.setSupportDir({});
}

std::string formatRelocateMsg(const WorkspaceRelocator::Result& result) {
    std::string message = std::to_string(result.filesCopied) + " file(s) moved";
    if (result.filesSkipped > 0)
        message += ", " + std::to_string(result.filesSkipped) + " skipped";
    if (!result.skippedCategories.empty()) {
        message += " (";
        for (size_t index = 0; index < result.skippedCategories.size(); ++index) {
            if (index > 0)
                message += ", ";
            message += result.skippedCategories[index];
        }
        message += " overridden, not moved)";
    }
    return message;
}

} // namespace

void Application::initWiring() {
    wireImportPipeline();
    wireWorkshop();
    wireCncPanels(); // implemented in application_wiring_cnc.cpp
    wirePropertiesPanel();
    wireMaterialsPanel();
    wireMenuActions();
}

void Application::wirePropertiesPanel() {
    if (!m_uiManager->propertiesPanel())
        return;

    m_uiManager->propertiesPanel()->setOnMeshModified([this]() {
        auto mesh = m_workspace->getFocusedMesh();
        if (mesh && m_uiManager->viewportPanel())
            m_uiManager->viewportPanel()->setMesh(mesh);
    });
    m_uiManager->propertiesPanel()->setOnColorChanged([this](const Color& color) {
        if (m_uiManager->viewportPanel())
            m_uiManager->viewportPanel()->renderSettings().objectColor = color;
    });
    m_uiManager->propertiesPanel()->setOnGrainDirectionChanged([this](float degrees) {
        auto mesh = m_workspace->getFocusedMesh();
        if (!mesh)
            return;
        mesh->generatePlanarUVs(degrees);
        if (m_uiManager->viewportPanel())
            m_uiManager->viewportPanel()->setMesh(mesh);
    });
    m_uiManager->propertiesPanel()->setOnMaterialRemoved([this]() {
        if (m_materialManager && m_focusedModelId > 0)
            m_materialManager->clearMaterialAssignment(m_focusedModelId);
        m_activeMaterialTexture.reset();
        m_activeMaterialId = -1;
        if (m_uiManager->viewportPanel())
            m_uiManager->viewportPanel()->setMaterialTexture(nullptr);
    });
}

void Application::wireMaterialsPanel() {
    if (!m_uiManager->materialsPanel())
        return;

    m_uiManager->materialsPanel()->setOnMaterialAssigned(
        [this](int64_t materialId) { assignMaterialToCurrentModel(materialId); });

    m_uiManager->materialsPanel()->setOnGenerate([this](const std::string& prompt) {
        std::string endpoint = Config::instance().getLMStudioEndpoint();
        if (endpoint.empty()) {
            log::warning("Application",
                         "LM Studio endpoint not set. Configure it in Settings > General.");
            ToastManager::instance().show(ToastType::Warning,
                                          "LM Studio Missing",
                                          "Set your LM Studio endpoint in Settings.");
            if (m_uiManager->materialsPanel())
                m_uiManager->materialsPanel()->setGenerating(false);
            return;
        }

        std::thread([this, prompt, endpoint]() {
            auto result = m_lmStudioService->generate(prompt, endpoint);
            m_mainThreadQueue->enqueue([this, result]() {
                if (result.success) {
                    log::infof("Application",
                               "AI generated material: %s",
                               result.record.name.c_str());
                    ToastManager::instance().show(ToastType::Success,
                                                  "Material Generated",
                                                  "Review and save: " + result.record.name);
                    if (m_uiManager->materialsPanel()) {
                        m_uiManager->materialsPanel()->setGeneratedResult(result.record,
                                                                          result.dwmatPath);
                    }
                } else {
                    log::errorf("Application",
                                "Material generation failed: %s",
                                result.error.c_str());
                    ToastManager::instance().show(ToastType::Error,
                                                  "Generation Failed",
                                                  result.error);
                    if (m_uiManager->materialsPanel())
                        m_uiManager->materialsPanel()->setGenerating(false);
                }
            });
        }).detach();
    });
}

std::string Application::handleResetToDefaults() {
    auto result = paths::resetUserStateToDefaults();
    if (!result.success) {
        return result.error.empty() ? "Failed to reset Digital Workshop user data."
                                    : result.error;
    }

    m_skipWorkspaceSaveOnShutdown = true;
    m_running = false;
    return {};
}

void Application::wireToolsMenu() {
    m_uiManager->setOnLibraryMaintenance([this]() {
        if (m_uiManager->maintenanceDialog())
            m_uiManager->maintenanceDialog()->open();
    });
    m_uiManager->setOnStartBackgroundTagging([this]() {
        if (!m_backgroundTagger)
            return;
        if (m_backgroundTagger->isActive()) {
            ToastManager::instance().show(ToastType::Info,
                                          "AI Tagging",
                                          "Background tagging is already running");
            return;
        }
        std::string endpoint;
        std::string model;
        if (!prepareAiTagging(endpoint, model))
            return;
        m_backgroundTagger->start(endpoint, model, BackgroundTaggerMode::SmartRetag);
        ToastManager::instance().show(ToastType::Info, "AI Tagging", "Background tagging started");
    });
    m_uiManager->setOnStopBackgroundTagging([this]() {
        if (!m_backgroundTagger || !m_backgroundTagger->isActive()) {
            ToastManager::instance().show(ToastType::Info,
                                          "AI Tagging",
                                          "Background tagging is not running");
            return;
        }
        m_backgroundTagger->stop();
        ToastManager::instance().show(ToastType::Info,
                                      "AI Tagging",
                                      "Stopping after the current model");
    });
    if (m_uiManager->maintenanceDialog()) {
        m_uiManager->maintenanceDialog()->setOnRun([this]() -> MaintenanceReport {
            auto report = m_libraryManager->runMaintenance();
            if (m_uiManager->libraryPanel())
                m_uiManager->libraryPanel()->refresh();
            int total = report.categoriesSplit + report.categoriesRemoved + report.tagsDeduped +
                        report.thumbnailsCleared + report.ftsRebuilt;
            ToastManager::instance().show(total > 0 ? ToastType::Success : ToastType::Info,
                                          "Maintenance Complete",
                                          total > 0 ? std::to_string(total) + " issue(s) fixed"
                                                    : "No issues found");
            return report;
        });
    }
    m_uiManager->setOnRelocateWorkspace([this]() { handleRelocateWorkspace(); });
    m_uiManager->setOnLocateMissingFiles([this]() { handleLocateMissingFiles(); });

    m_uiManager->setOnExportSettings([this]() {
        m_uiManager->fileDialog()->showSave(
            "Export Settings",
            {{"DW Settings", "*.dwsettings"}},
            "digital_workshop.dwsettings",
            [](const std::string& path) {
                if (path.empty())
                    return;
                if (exportSettings(Path(path))) {
                    ToastManager::instance().show(ToastType::Success, "Settings exported");
                } else {
                    ToastManager::instance().show(ToastType::Error, "Failed to export settings");
                }
            });
    });

    m_uiManager->setOnImportSettings([this]() {
        m_uiManager->fileDialog()->showOpen("Import Settings",
                                            {{"DW Settings", "*.dwsettings"}},
                                            [this](const std::string& path) {
                                                if (path.empty())
                                                    return;
                                                m_uiManager->settingsImportDialog()->open(path);
                                            });
    });
}

void Application::handleRelocateWorkspace() {
    Path currentRoot = Config::instance().getEffectiveWorkspaceRoot();
    m_uiManager->fileDialog()->showFolder(
        "Select New Workspace Location", [this, currentRoot](const std::string& destination) {
            if (destination.empty())
                return;
            Path destinationRoot(destination);
            std::error_code error;
            if (fs::equivalent(currentRoot, destinationRoot, error)) {
                ToastManager::instance().show(ToastType::Info,
                                              "Relocate Workspace",
                                              "Already at that location");
                return;
            }
            int fileCount = WorkspaceRelocator::countFiles(currentRoot);
            if (fileCount == 0) {
                auto& config = Config::instance();
                config.setWorkspaceRoot(destinationRoot);
                clearDirOverrides(config);
                config.save();
                ToastManager::instance().show(ToastType::Success,
                                              "Workspace Relocated",
                                              "Workspace root updated (no files to move)");
                return;
            }
            auto* progress = m_uiManager->progressDialog();
            progress->start("Relocating Workspace...", fileCount);
            WorkspaceRelocator::Options options{currentRoot, destinationRoot, true};
            std::thread([this, options, progress]() {
                auto result = WorkspaceRelocator::relocate(
                    options,
                    [progress](const std::string& file) { progress->advance(file); },
                    [progress]() { return progress->isCancelled(); });
                m_mainThreadQueue->enqueue([this, result, options]() {
                    m_uiManager->progressDialog()->finish();
                    if (result.success) {
                        auto& config = Config::instance();
                        config.setWorkspaceRoot(options.destRoot);
                        clearDirOverrides(config);
                        config.save();
                        ToastManager::instance().show(ToastType::Success,
                                                      "Workspace Relocated",
                                                      formatRelocateMsg(result));
                    } else {
                        ToastManager::instance().show(ToastType::Error,
                                                      "Relocation Failed",
                                                      result.error);
                    }
                });
            }).detach();
        });
}

void Application::handleLocateMissingFiles() {
    PathRecovery recovery(*m_modelRepo, *m_gcodeRepo);
    auto missing = recovery.findMissing();
    if (missing.empty()) {
        ToastManager::instance().show(ToastType::Info, "Missing Files", "All files accounted for");
        return;
    }
    auto missingPtr = std::make_shared<std::vector<MissingFile>>(std::move(missing));
    ToastManager::instance().show(ToastType::Warning,
                                  "Missing Files",
                                  std::to_string(missingPtr->size()) +
                                      " file(s) not found — locate one to recover");
    m_uiManager->fileDialog()->showOpen(
        "Locate a Missing File",
        {{"All Files", "*"}},
        [this, missingPtr](const std::string& selected) {
            if (selected.empty())
                return;
            Path selectedPath(selected);
            std::string selectedName = selectedPath.filename().string();
            const MissingFile* match = nullptr;
            for (const auto& missingFile : *missingPtr) {
                if (missingFile.resolvedPath.filename().string() == selectedName) {
                    match = &missingFile;
                    break;
                }
            }
            if (!match) {
                ToastManager::instance().show(ToastType::Error,
                                              "Missing Files",
                                              "No missing file matches that name");
                return;
            }
            PathRecovery pathRecovery(*m_modelRepo, *m_gcodeRepo);
            auto recovered =
                pathRecovery.recoverFromRelocated(*match, selectedPath, *missingPtr);
            if (recovered.empty()) {
                ToastManager::instance().show(ToastType::Warning,
                                              "Missing Files",
                                              "Could not recover any files");
                return;
            }
            int updated = pathRecovery.applyRecoveries(recovered);
            ToastManager::instance().show(ToastType::Success,
                                          "Files Recovered",
                                          std::to_string(updated) + " of " +
                                              std::to_string(missingPtr->size()) +
                                              " file(s) recovered");
            if (m_uiManager->libraryPanel())
                m_uiManager->libraryPanel()->refresh();
        });
}

} // namespace dw
