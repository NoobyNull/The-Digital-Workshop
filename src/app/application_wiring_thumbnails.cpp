// Library thumbnail regeneration wiring and background execution.

#include "app/application.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/library/library_manager.h"
#include "core/loaders/loader_factory.h"
#include "core/paths/path_resolver.h"
#include "core/threading/main_thread_queue.h"
#include "managers/ui_manager.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/panels/library_panel.h"
#include "ui/widgets/toast.h"

namespace dw {

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
        ToastManager::instance().show(ToastType::Error,
                                      "Thumbnail Failed",
                                      "Model not found in database");
        return;
    }
    Path filePath = PathResolver::resolve(record->filePath, PathCategory::Support);
    std::string modelName = record->name;
    ToastManager::instance().show(ToastType::Info, "Regenerating Thumbnail", modelName);
    std::thread([this, filePath, modelId, modelName]() {
        auto result = LoaderFactory::load(filePath);
        if (!result) {
            m_mainThreadQueue->enqueue([modelName, error = result.error]() {
                ToastManager::instance().show(ToastType::Error,
                                              "Thumbnail Failed",
                                              modelName + ": " +
                                                  (error.empty() ? "failed to load file" : error));
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
            ToastManager::instance().show(ok ? ToastType::Success : ToastType::Error,
                                          ok ? "Thumbnail Updated" : "Thumbnail Failed",
                                          ok ? modelName : modelName + ": generation failed");
        });
    }).detach();
}

void Application::regenerateBatchThumbnails(const std::vector<int64_t>& modelIds) {
    auto* progressDlg = m_uiManager->progressDialog();
    if (!progressDlg)
        return;
    struct BatchItem {
        int64_t id;
        Path filePath;
        std::string name;
    };
    auto items = std::make_shared<std::vector<BatchItem>>();
    items->reserve(modelIds.size());
    for (int64_t id : modelIds) {
        auto record = m_libraryManager->getModel(id);
        if (record) {
            items->push_back(
                {id, PathResolver::resolve(record->filePath, PathCategory::Support), record->name});
        }
    }
    if (items->empty())
        return;
    progressDlg->start("Regenerating Thumbnails", static_cast<int>(items->size()));
    std::thread([this, items, progressDlg]() {
        for (const auto& item : *items) {
            if (progressDlg->isCancelled())
                break;
            auto result = LoaderFactory::load(item.filePath);
            if (!result) {
                m_mainThreadQueue->enqueue([name = item.name, error = result.error]() {
                    ToastManager::instance().show(
                        ToastType::Error,
                        "Thumbnail Failed",
                        name + ": " + (error.empty() ? "failed to load file" : error));
                });
                progressDlg->advance(item.name);
                continue;
            }
            auto mesh = result.mesh;
            auto modelId = item.id;
            auto modelName = item.name;
            auto generated = std::make_shared<std::promise<bool>>();
            auto generatedFuture = generated->get_future();
            m_mainThreadQueue->enqueue([this, mesh, modelId, modelName, generated]() {
                bool ok = generateMaterialThumbnail(modelId, *mesh);
                if (m_uiManager->libraryPanel())
                    m_uiManager->libraryPanel()->invalidateThumbnail(modelId);
                if (!ok) {
                    ToastManager::instance().show(ToastType::Error,
                                                  "Thumbnail Failed",
                                                  modelName + ": generation failed");
                }
                generated->set_value(ok);
            });
            while (!progressDlg->isCancelled() &&
                   generatedFuture.wait_for(std::chrono::milliseconds(100)) !=
                       std::future_status::ready) {}
            if (generatedFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                (void)generatedFuture.get();
            }
            progressDlg->advance(item.name);
        }
        m_mainThreadQueue->enqueue([this, progressDlg]() {
            progressDlg->finish();
            if (m_uiManager->libraryPanel())
                m_uiManager->libraryPanel()->refresh();
            ToastManager::instance().show(ToastType::Success,
                                          "Thumbnails Updated",
                                          "Batch regeneration complete");
        });
    }).detach();
}

} // namespace dw
