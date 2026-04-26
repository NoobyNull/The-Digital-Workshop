// Application wiring — panel callbacks, menu setup, dialog setup.
// CNC/GCode wiring is in application_wiring_cnc.cpp.
// Model/material callbacks are in application_callbacks.cpp.

#include "app/application.h"

#include <string>
#include <thread>
#include <vector>

#include "app/workspace.h"
#include "core/config/config.h"
#include "core/config/settings_archive.h"
#include "core/database/connection_pool.h"
#include "core/database/cut_plan_repository.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/import/background_tagger.h"
#include "core/import/import_queue.h"
#include "core/library/library_manager.h"
#include "core/loaders/loader_factory.h"
#include "core/materials/gemini_descriptor_service.h"
#include "core/materials/gemini_material_service.h"
#include "core/materials/material_manager.h"
#include "core/paths/path_recovery.h"
#include "core/paths/path_resolver.h"
#include "core/threading/main_thread_queue.h"
#include "core/utils/log.h"
#include "core/workspace/workspace_relocator.h"
#include "managers/config_manager.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "render/texture.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/import_options_dialog.h"
#include "ui/dialogs/import_summary_dialog.h"
#include "ui/dialogs/maintenance_dialog.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/dialogs/tag_image_dialog.h"
#include "ui/dialogs/settings_import_dialog.h"
#include "ui/dialogs/tagger_shutdown_dialog.h"
#include "ui/panels/cost_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/direct_carve_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/start_page.h"
#include "ui/panels/viewport_panel.h"
#include "core/optimizer/cut_list_file.h"
#include "core/project/project.h"
#include "ui/widgets/toast.h"

namespace dw {

namespace {

// Reset all per-category directory overrides to default (workspace-relative).
void clearDirOverrides(Config& config) {
    config.setModelsDir({});
    config.setProjectsDir({});
    config.setMaterialsDir({});
    config.setGCodeDir({});
    config.setSupportDir({});
}

// Persist AI descriptor results to the library database.
void persistTagResults(LibraryManager* libMgr, int64_t modelId, const DescriptorResult& result) {
    libMgr->updateDescriptor(modelId, result.title, result.description, result.hoverNarrative);
    auto existing = libMgr->getModel(modelId);
    if (existing) {
        auto tags = existing->tags;
        for (const auto& kw : result.keywords)
            tags.push_back(kw);
        for (const auto& assoc : result.associations)
            tags.push_back(assoc);
        libMgr->updateTags(modelId, tags);
    }
    if (!result.categories.empty())
        libMgr->resolveAndAssignCategories(modelId, result.categories);
}

// Format the result message for workspace relocation.
std::string formatRelocateMsg(const WorkspaceRelocator::Result& result) {
    std::string msg = std::to_string(result.filesCopied) + " file(s) moved";
    if (result.filesSkipped > 0)
        msg += ", " + std::to_string(result.filesSkipped) + " skipped";
    if (!result.skippedCategories.empty()) {
        msg += " (";
        for (size_t i = 0; i < result.skippedCategories.size(); ++i) {
            if (i > 0) msg += ", ";
            msg += result.skippedCategories[i];
        }
        msg += " overridden, not moved)";
    }
    return msg;
}

} // anonymous namespace

void Application::initWiring() {
    wireImportPipeline();
    wireStartPage();
    wireLibraryPanel();
    wireProjectPanel();
    wireCncPanels();        // implemented in application_wiring_cnc.cpp
    wirePropertiesPanel();
    wireMaterialsPanel();
    wireMenuActions();
}

void Application::wireImportPipeline() {
    m_uiManager->setImportCancelCallback([this]() {
        if (m_importQueue) m_importQueue->cancel();
    });
    m_importQueue->setOnBatchComplete([this](const ImportBatchSummary& summary) {
        m_mainThreadQueue->enqueue([this, summary]() {
            if (Config::instance().getShowImportErrorToasts()) {
                if (summary.failedCount > 0)
                    ToastManager::instance().show(
                        ToastType::Error, "Import Errors",
                        std::to_string(summary.failedCount) + " file(s) failed to import");
                if (summary.successCount > 0)
                    ToastManager::instance().show(
                        ToastType::Success, "Import Complete",
                        std::to_string(summary.successCount) + " file(s) imported successfully");
            }
            if (summary.duplicateCount > 0)
                m_uiManager->showImportSummary(summary);
            if (m_importQueue->queueForTagging() && m_backgroundTagger &&
                !m_backgroundTagger->isActive()) {
                auto apiKey = Config::instance().getGeminiApiKey();
                if (!apiKey.empty()) m_backgroundTagger->start(apiKey);
            }
        });
    });
    m_fileIOManager->setImportOptionsDialog(m_uiManager->importOptionsDialog());
    if (m_uiManager->importOptionsDialog()) {
        m_uiManager->importOptionsDialog()->setOnConfirm(
            [this](FileHandlingMode mode, bool tagAfterImport, const std::vector<Path>& paths) {
                if (m_importQueue && !paths.empty()) {
                    m_importQueue->setQueueForTagging(tagAfterImport);
                    m_importQueue->enqueue(paths, mode);
                }
            });
    }
    if (m_uiManager->importSummaryDialog()) {
        m_uiManager->importSummaryDialog()->setOnReimport(
            [this](std::vector<DuplicateRecord> selected) {
                if (m_importQueue && !selected.empty())
                    m_importQueue->enqueueForReimport(selected);
            });
    }
}

void Application::wireStartPage() {
    if (!m_uiManager->startPage())
        return;

    auto* sp = m_uiManager->startPage();
    auto hideStart = [this](bool show) {
        m_uiManager->showStartPage() = show;
    };
    sp->setOnNewProject([this, hideStart]() { m_fileIOManager->newProject(hideStart); });
    sp->setOnOpenProject([this, hideStart]() { m_fileIOManager->openProject(hideStart); });
    sp->setOnImportModel([this]() {
        m_fileIOManager->importModel();
        m_uiManager->showStartPage() = false;
    });
    sp->setOnOpenRecentProject([this, hideStart](const Path& path) {
        m_fileIOManager->openRecentProject(path, hideStart);
    });
    sp->setOnWorkspaceModeChanged([this](int mode) {
        m_uiManager->setWorkspaceMode(mode == 1 ? WorkspaceMode::CNC : WorkspaceMode::Model);
    });
}

void Application::wireLibraryPanel() {
    if (!m_uiManager->libraryPanel())
        return;

    m_uiManager->libraryPanel()->setProjectManager(m_projectManager.get());
    m_uiManager->libraryPanel()->setOnGCodeAddToProject(
        [this](const std::vector<int64_t>& gcodeIds) {
            if (!m_projectManager || !m_projectManager->currentProject() || !m_gcodeRepo)
                return;
            i64 pid = m_projectManager->currentProject()->id();
            int sortOrder = static_cast<int>(m_gcodeRepo->findByProject(pid).size());
            for (int64_t gid : gcodeIds) {
                if (!m_gcodeRepo->isInProject(pid, gid)) {
                    m_gcodeRepo->addToProject(pid, gid, sortOrder++);
                }
            }
        });

    m_uiManager->libraryPanel()->setOnModelSelected([this](int64_t modelId) {
        if (!m_libraryManager)
            return;
        auto record = m_libraryManager->getModel(modelId);
        if (record && m_uiManager->propertiesPanel())
            m_uiManager->propertiesPanel()->setModelRecord(*record);
    });
    m_uiManager->libraryPanel()->setOnModelOpened(
        [this](int64_t modelId) { onModelSelected(modelId); });

    m_uiManager->libraryPanel()->setOnRegenerateThumbnail(
        [this](const std::vector<int64_t>& modelIds) { regenerateThumbnails(modelIds); });

    m_uiManager->libraryPanel()->setOnAssignDefaultMaterial([this](int64_t modelId) {
        i64 defaultMatId = Config::instance().getDefaultMaterialId();
        if (defaultMatId <= 0 || !m_materialManager)
            return;
        auto mat = m_materialManager->getMaterial(defaultMatId);
        if (!mat)
            return;
        m_materialManager->assignMaterialToModel(defaultMatId, modelId);
    });

    wireTagDialog();
}

void Application::wireProjectPanel() {
    auto* pp = m_uiManager->projectPanel();
    if (!pp) return;
    auto hideStart = [this](bool show) { m_uiManager->showStartPage() = show; };
    pp->setOnModelSelected([this](int64_t modelId) { onModelSelected(modelId); });
    pp->setOpenProjectCallback([this, hideStart]() { m_fileIOManager->openProject(hideStart); });
    pp->setSaveProjectCallback([this]() { m_fileIOManager->saveProject(); });
    pp->setOnOpenRecentProject([this, hideStart](const Path& path) {
        m_fileIOManager->openRecentProject(path, hideStart);
    });
    pp->setExportProjectCallback([this]() { m_fileIOManager->exportProjectArchive(); });
    pp->setOnGCodeSelected([this](i64 gcodeId) {
        if (!m_gcodeRepo) return;
        auto rec = m_gcodeRepo->findById(gcodeId);
        if (rec && m_uiManager->gcodePanel()) {
            m_uiManager->openWindow("gcode_viewer");
            m_uiManager->gcodePanel()->loadFile(
                PathResolver::resolve(rec->filePath, PathCategory::GCode).string());
        }
    });
    pp->setOnOperationSelected([this](const ProjectOpenItem& item) {
        auto* directCarve = m_uiManager->directCarvePanel();
        if (!directCarve || !m_projectManager) return;

        for (const auto& candidate : m_projectManager->currentOpenItems()) {
            if (item.parentItemId.has_value() &&
                candidate.id == *item.parentItemId &&
                candidate.itemType == ProjectOpenItemType::Model &&
                candidate.sourceId.has_value()) {
                onModelSelected(*candidate.sourceId);
                break;
            }
        }

        m_uiManager->openWindow("direct_carve");
        (void)directCarve->loadOperationOpenItem(item);
    });
    pp->setOnMaterialSelected([this](i64 id) {
        if (auto* p = m_uiManager->materialsPanel()) {
            m_uiManager->openWindow("materials");
            p->selectMaterial(id);
        }
    });
    pp->setOnCostSelected([this](i64 id) {
        if (auto* p = m_uiManager->costPanel()) {
            m_uiManager->openWindow("project_costing");
            p->selectRecord(id);
        }
    });
    pp->setOnCutPlanSelected([this](i64 planId) {
        if (!m_cutPlanRepo || !m_cutListFile) return;
        auto rec = m_cutPlanRepo->findById(planId);
        if (!rec || !m_uiManager->cutOptimizerPanel()) return;
        CutListFile::LoadResult lr;
        lr.name = rec->name;
        lr.algorithm = rec->algorithm;
        lr.allowRotation = rec->allowRotation;
        lr.kerf = rec->kerf;
        lr.margin = rec->margin;
        if (!rec->sheetConfigJson.empty())
            lr.sheet = CutPlanRepository::jsonToSheet(rec->sheetConfigJson);
        if (!rec->partsJson.empty())
            lr.parts = CutPlanRepository::jsonToParts(rec->partsJson);
        if (!rec->resultJson.empty())
            lr.result = CutPlanRepository::jsonToCutPlan(rec->resultJson);
        m_uiManager->openWindow("cut_optimizer");
        m_uiManager->cutOptimizerPanel()->loadCutPlan(lr);
    });
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
        if (m_uiManager->viewportPanel()) {
            m_uiManager->viewportPanel()->setMaterialTexture(nullptr);
        }
    });
}

void Application::wireMaterialsPanel() {
    if (!m_uiManager->materialsPanel())
        return;

    m_uiManager->materialsPanel()->setOnMaterialAssigned(
        [this](int64_t materialId) { assignMaterialToCurrentModel(materialId); });

    m_uiManager->materialsPanel()->setOnGenerate([this](const std::string& prompt) {
        std::string apiKey = Config::instance().getGeminiApiKey();
        if (apiKey.empty()) {
            log::warning("Application",
                         "Gemini API key not set. Configure it in Settings > General.");
            ToastManager::instance().show(ToastType::Warning,
                                          "API Key Missing",
                                          "Set your Gemini API key in Settings.");
            if (m_uiManager->materialsPanel())
                m_uiManager->materialsPanel()->setGenerating(false);
            return;
        }

        std::thread([this, prompt, apiKey]() {
            auto result = m_geminiService->generate(prompt, apiKey);
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

void Application::wireMenuActions() {
    // Wire UIManager action callbacks (menu bar and keyboard shortcuts)
    auto hideStart = [this](bool show) {
        m_uiManager->showStartPage() = show;
    };
    m_uiManager->setOnNewProject([this, hideStart]() { m_fileIOManager->newProject(hideStart); });
    m_uiManager->setOnOpenProject([this, hideStart]() { m_fileIOManager->openProject(hideStart); });
    m_uiManager->setOnSaveProject([this]() { m_fileIOManager->saveProject(); });
    m_uiManager->setOnImportModel([this]() { m_fileIOManager->importModel(); });
    m_uiManager->setOnExportModel([this]() { m_fileIOManager->exportModel(); });
    m_uiManager->setOnImportProjectArchive(
        [this, hideStart]() { m_fileIOManager->importProjectArchive(hideStart); });
    m_uiManager->setOnQuit([this]() { quit(); });
    m_uiManager->setOnSpawnSettings([this]() { m_configManager->spawnSettingsApp(); });

    wireToolsMenu();

    // Wire tagger shutdown dialog
    if (m_uiManager->taggerShutdownDialog()) {
        m_uiManager->taggerShutdownDialog()->setOnQuit([this]() { m_running = false; });
    }
}

void Application::regenerateThumbnails(const std::vector<int64_t>& modelIds) {
    if (!m_libraryManager || modelIds.empty())
        return;
    if (modelIds.size() == 1)
        regenerateSingleThumbnail(modelIds[0]);
    else
        regenerateBatchThumbnails(modelIds);
}

void Application::regenerateSingleThumbnail(int64_t modelId) {
    auto record = m_libraryManager->getModel(modelId);
    if (!record) {
        ToastManager::instance().show(
            ToastType::Error, "Thumbnail Failed", "Model not found in database");
        return;
    }
    Path filePath = PathResolver::resolve(record->filePath, PathCategory::Support);
    std::string modelName = record->name;
    ToastManager::instance().show(ToastType::Info, "Regenerating Thumbnail", modelName);
    std::thread([this, filePath, modelId, modelName]() {
        auto result = LoaderFactory::load(filePath);
        if (!result) {
            m_mainThreadQueue->enqueue([modelName, error = result.error]() {
                ToastManager::instance().show(
                    ToastType::Error, "Thumbnail Failed",
                    modelName + ": " + (error.empty() ? "failed to load file" : error));
            });
            return;
        }
        auto mesh = result.mesh;
        m_mainThreadQueue->enqueue([this, mesh, modelId, modelName]() {
            bool ok = generateMaterialThumbnail(modelId, *mesh);
            if (m_uiManager->libraryPanel()) {
                m_uiManager->libraryPanel()->invalidateThumbnail(modelId);
                m_uiManager->libraryPanel()->refresh();
            }
            ToastManager::instance().show(
                ok ? ToastType::Success : ToastType::Error,
                ok ? "Thumbnail Updated" : "Thumbnail Failed",
                ok ? modelName : modelName + ": generation failed");
        });
    }).detach();
}

void Application::regenerateBatchThumbnails(const std::vector<int64_t>& modelIds) {
    auto* progressDlg = m_uiManager->progressDialog();
    if (!progressDlg) return;
    struct BatchItem { int64_t id; Path filePath; std::string name; };
    auto items = std::make_shared<std::vector<BatchItem>>();
    items->reserve(modelIds.size());
    for (int64_t id : modelIds) {
        auto record = m_libraryManager->getModel(id);
        if (record)
            items->push_back({id, PathResolver::resolve(record->filePath, PathCategory::Support),
                              record->name});
    }
    if (items->empty())
        return;
    progressDlg->start("Regenerating Thumbnails", static_cast<int>(items->size()));
    std::thread([this, items, progressDlg]() {
        for (const auto& item : *items) {
            if (progressDlg->isCancelled()) break;
            auto result = LoaderFactory::load(item.filePath);
            if (!result) {
                m_mainThreadQueue->enqueue([name = item.name, error = result.error]() {
                    ToastManager::instance().show(
                        ToastType::Error, "Thumbnail Failed",
                        name + ": " + (error.empty() ? "failed to load file" : error));
                });
                progressDlg->advance(item.name);
                continue;
            }
            auto mesh = result.mesh;
            auto modelId = item.id;
            auto modelName = item.name;
            m_mainThreadQueue->enqueue([this, mesh, modelId, modelName]() {
                bool ok = generateMaterialThumbnail(modelId, *mesh);
                if (m_uiManager->libraryPanel())
                    m_uiManager->libraryPanel()->invalidateThumbnail(modelId);
                if (!ok)
                    ToastManager::instance().show(
                        ToastType::Error, "Thumbnail Failed", modelName + ": generation failed");
            });
            progressDlg->advance(item.name);
        }
        m_mainThreadQueue->enqueue([this, progressDlg]() {
            progressDlg->finish();
            if (m_uiManager->libraryPanel())
                m_uiManager->libraryPanel()->refresh();
            ToastManager::instance().show(
                ToastType::Success, "Thumbnails Updated", "Batch regeneration complete");
        });
    }).detach();
}

void Application::wireTagDialog() {
    auto* tagDlg = m_uiManager->tagImageDialog();
    if (!tagDlg) return;

    tagDlg->setOnRequest([this, tagDlg](int64_t modelId) {
        if (!m_descriptorService || !m_mainThreadQueue) return;
        std::string apiKey = Config::instance().getGeminiApiKey();
        if (apiKey.empty()) {
            log::warning("App", "Gemini API key not configured");
            DescriptorResult err;
            err.error = "Gemini API key not configured";
            tagDlg->setResult(err);
            return;
        }
        auto record = m_libraryManager->getModel(modelId);
        if (!record || record->thumbnailPath.empty()) {
            DescriptorResult err;
            err.error = "Model has no thumbnail";
            tagDlg->setResult(err);
            return;
        }
        auto* svc = m_descriptorService.get();
        auto* mtq = m_mainThreadQueue.get();
        std::string thumbPath = record->thumbnailPath.string();
        std::thread([svc, mtq, tagDlg, thumbPath, apiKey]() {
            auto result = svc->describe(thumbPath, apiKey);
            mtq->enqueue([tagDlg, result]() { tagDlg->setResult(result); });
        }).detach();
    });
    tagDlg->setOnSave([this](int64_t modelId, const DescriptorResult& result) {
        persistTagResults(m_libraryManager.get(), modelId, result);
        m_uiManager->libraryPanel()->refresh();
        m_uiManager->libraryPanel()->invalidateThumbnail(modelId);
        if (m_uiManager->propertiesPanel()) {
            auto updated = m_libraryManager->getModel(modelId);
            if (updated)
                m_uiManager->propertiesPanel()->setModelRecord(*updated);
        }
        ToastManager::instance().show(ToastType::Success, "Tagged", result.title);
        log::infof("App", "Tagged model %lld as: %s",
                   static_cast<long long>(modelId), result.title.c_str());
    });
    m_uiManager->libraryPanel()->setOnTagImage(
        [this](const std::vector<int64_t>& modelIds) { handleTagImage(modelIds); });
}

void Application::handleTagImage(const std::vector<int64_t>& modelIds) {
    if (modelIds.empty()) return;
    auto* tagDlg = m_uiManager->tagImageDialog();
    if (!tagDlg) return;

    if (modelIds.size() == 1) {
        auto record = m_libraryManager->getModel(modelIds[0]);
        if (!record) return;
        GLuint tex = m_uiManager->libraryPanel()->getThumbnailTextureForModel(modelIds[0]);
        tagDlg->open(*record, tex);
        return;
    }
    std::string apiKey = Config::instance().getGeminiApiKey();
    if (apiKey.empty()) {
        log::warning("App", "Gemini API key not configured");
        return;
    }
    auto* svc = m_descriptorService.get();
    auto* libMgr = m_libraryManager.get();
    auto* mtq = m_mainThreadQueue.get();
    auto* libPanel = m_uiManager->libraryPanel();
    size_t count = modelIds.size();
    for (int64_t modelId : modelIds) {
        auto record = m_libraryManager->getModel(modelId);
        if (!record || record->thumbnailPath.empty()) continue;
        std::string thumbPath = record->thumbnailPath.string();
        std::string modelName = record->name;
        std::thread([svc, libMgr, mtq, libPanel, modelId, modelName, thumbPath, apiKey]() {
            auto result = svc->describe(thumbPath, apiKey);
            mtq->enqueue([libMgr, libPanel, modelId, modelName, result]() {
                if (result.success) {
                    persistTagResults(libMgr, modelId, result);
                    libPanel->refresh();
                    libPanel->invalidateThumbnail(modelId);
                    ToastManager::instance().show(ToastType::Success, "Tagged", result.title);
                    log::infof("App", "Tagged %s as: %s",
                               modelName.c_str(), result.title.c_str());
                } else {
                    log::warningf("App", "Descriptor failed for %s: %s",
                                  modelName.c_str(), result.error.c_str());
                }
            });
        }).detach();
    }
    ToastManager::instance().show(
        ToastType::Info, "Tagging", "Classifying " + std::to_string(count) + " models...");
}

void Application::wireToolsMenu() {
    m_uiManager->setOnLibraryMaintenance([this]() {
        if (m_uiManager->maintenanceDialog())
            m_uiManager->maintenanceDialog()->open();
    });
    if (m_uiManager->maintenanceDialog()) {
        m_uiManager->maintenanceDialog()->setOnRun([this]() -> MaintenanceReport {
            auto report = m_libraryManager->runMaintenance();
            if (m_uiManager->libraryPanel())
                m_uiManager->libraryPanel()->refresh();
            int total = report.categoriesSplit + report.categoriesRemoved + report.tagsDeduped +
                        report.thumbnailsCleared + report.ftsRebuilt;
            ToastManager::instance().show(
                total > 0 ? ToastType::Success : ToastType::Info,
                "Maintenance Complete",
                total > 0 ? std::to_string(total) + " issue(s) fixed" : "No issues found");
            return report;
        });
    }
    m_uiManager->setOnRelocateWorkspace([this]() { handleRelocateWorkspace(); });
    m_uiManager->setOnLocateMissingFiles([this]() { handleLocateMissingFiles(); });

    // Settings export/import
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
                    ToastManager::instance().show(ToastType::Error,
                                                  "Failed to export settings");
                }
            });
    });

    m_uiManager->setOnImportSettings([this]() {
        m_uiManager->fileDialog()->showOpen(
            "Import Settings",
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
    m_uiManager->fileDialog()->showFolder("Select New Workspace Location",
        [this, currentRoot](const std::string& dest) {
            if (dest.empty()) return;
            Path destRoot(dest);
            std::error_code ec;
            if (fs::equivalent(currentRoot, destRoot, ec)) {
                ToastManager::instance().show(
                    ToastType::Info, "Relocate Workspace", "Already at that location");
                return;
            }
            int fileCount = WorkspaceRelocator::countFiles(currentRoot);
            if (fileCount == 0) {
                auto& config = Config::instance();
                config.setWorkspaceRoot(destRoot);
                clearDirOverrides(config);
                config.save();
                ToastManager::instance().show(ToastType::Success, "Workspace Relocated",
                                              "Workspace root updated (no files to move)");
                return;
            }
            auto* progress = m_uiManager->progressDialog();
            progress->start("Relocating Workspace...", fileCount);
            WorkspaceRelocator::Options opts{currentRoot, destRoot, true};
            std::thread([this, opts, progress]() {
                auto result = WorkspaceRelocator::relocate(
                    opts,
                    [progress](const std::string& file) { progress->advance(file); },
                    [progress]() { return progress->isCancelled(); });
                m_mainThreadQueue->enqueue([this, result, opts]() {
                    m_uiManager->progressDialog()->finish();
                    if (result.success) {
                        auto& config = Config::instance();
                        config.setWorkspaceRoot(opts.destRoot);
                        clearDirOverrides(config);
                        config.save();
                        ToastManager::instance().show(ToastType::Success, "Workspace Relocated",
                                                      formatRelocateMsg(result));
                    } else {
                        ToastManager::instance().show(
                            ToastType::Error, "Relocation Failed", result.error);
                    }
                });
            }).detach();
        });
}

void Application::handleLocateMissingFiles() {
    PathRecovery recovery(*m_modelRepo, *m_gcodeRepo);
    auto missing = recovery.findMissing();
    if (missing.empty()) {
        ToastManager::instance().show(
            ToastType::Info, "Missing Files", "All files accounted for");
        return;
    }
    auto missingPtr = std::make_shared<std::vector<MissingFile>>(std::move(missing));
    ToastManager::instance().show(
        ToastType::Warning, "Missing Files",
        std::to_string(missingPtr->size()) + " file(s) not found — locate one to recover");
    m_uiManager->fileDialog()->showOpen(
        "Locate a Missing File", {{"All Files", "*"}},
        [this, missingPtr](const std::string& selected) {
            if (selected.empty()) return;
            Path selectedPath(selected);
            std::string selectedName = selectedPath.filename().string();
            const MissingFile* match = nullptr;
            for (const auto& mf : *missingPtr) {
                if (mf.resolvedPath.filename().string() == selectedName) {
                    match = &mf;
                    break;
                }
            }
            if (!match) {
                ToastManager::instance().show(
                    ToastType::Error, "Missing Files", "No missing file matches that name");
                return;
            }
            PathRecovery pathRecovery(*m_modelRepo, *m_gcodeRepo);
            auto recovered =
                pathRecovery.recoverFromRelocated(*match, selectedPath, *missingPtr);
            if (recovered.empty()) {
                ToastManager::instance().show(
                    ToastType::Warning, "Missing Files", "Could not recover any files");
                return;
            }
            int updated = pathRecovery.applyRecoveries(recovered);
            ToastManager::instance().show(
                ToastType::Success, "Files Recovered",
                std::to_string(updated) + " of " +
                    std::to_string(missingPtr->size()) + " file(s) recovered");
            if (m_uiManager->libraryPanel())
                m_uiManager->libraryPanel()->refresh();
        });
}

} // namespace dw
