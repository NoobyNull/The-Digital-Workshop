// AI tagging wiring and smart-retag orchestration.

#include "app/application.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/ai/ollama_runtime.h"
#include "core/config/config.h"
#include "core/import/smart_tagger.h"
#include "core/library/library_manager.h"
#include "core/materials/lmstudio_descriptor_service.h"
#include "core/paths/app_paths.h"
#include "core/threading/main_thread_queue.h"
#include "core/utils/log.h"
#include "managers/ui_manager.h"
#include "render/texture.h"
#include "ui/dialogs/tag_image_dialog.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

void persistTagResults(LibraryManager* libMgr,
                       int64_t modelId,
                       const DescriptorResult& result) {
    libMgr->updateDescriptor(modelId, result.title, result.description, result.hoverNarrative);
    auto existing = libMgr->getModel(modelId);
    if (existing) {
        auto tags = existing->tags;
        for (const auto& keyword : result.keywords)
            tags.push_back(keyword);
        for (const auto& association : result.associations)
            tags.push_back(association);
        libMgr->updateTags(modelId, tags);
    }
    if (!result.categories.empty())
        libMgr->resolveAndAssignCategories(modelId, result.categories);
    libMgr->updateTagStatus(modelId, 2);
}

void resetAiTagStateForRetag(LibraryManager* libMgr, int64_t modelId) {
    if (libMgr)
        libMgr->clearAiClassification(modelId);
}

std::string utcTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

const char* descriptorStatusName(TagClassificationStatus status) {
    switch (status) {
    case TagClassificationStatus::FinalTag:
        return "final_tag";
    case TagClassificationStatus::RetryView:
        return "retry_view";
    case TagClassificationStatus::FallbackIsometric:
        return "fallback_isometric";
    case TagClassificationStatus::Unclassifiable:
        return "unclassifiable";
    }
    return "final_tag";
}

void appendManualTaggerLog(const std::string& line) {
    std::ofstream out(paths::getDataDir() / "tagger.log", std::ios::app);
    if (!out.is_open())
        return;
    out << utcTimestamp() << ' ' << line << '\n';
}

void appendManualTaggerResultLog(int64_t modelId,
                                 const std::string& modelName,
                                 const char* eventName,
                                 const DescriptorResult& result) {
    std::ostringstream line;
    line << eventName << " model_id=" << modelId << " name=\"" << modelName << "\""
         << " success=" << (result.success ? "true" : "false")
         << " confidence=" << result.confidence
         << " response_status=" << descriptorStatusName(result.status)
         << " needs_retag=" << (result.needsRetag ? "true" : "false")
         << " current_view=" << smart_tagging::thumbnailViewName(result.currentView)
         << " recommended_view=" << smart_tagging::thumbnailViewName(result.recommendedView)
         << " orientation_needs_rotation="
         << (result.orientation.needsRotation ? "true" : "false")
         << " orientation_rotate_degrees=" << result.orientation.rotateDegrees
         << " orientation_upright_view="
         << smart_tagging::thumbnailViewName(result.orientation.uprightView) << " reason=\""
         << result.viewReason << "\""
         << " orientation_reason=\"" << result.orientation.reason << "\""
         << " title=\"" << result.title << "\""
         << " error=\"" << result.error << "\"";
    appendManualTaggerLog(line.str());
}

void markManualUnclassifiable(DescriptorResult& result) {
    result.success = true;
    result.status = TagClassificationStatus::Unclassifiable;
    result.title.clear();
    result.description.clear();
    result.hoverNarrative.clear();
    result.keywords.clear();
    result.associations.clear();
    result.categories.clear();
    result.orientation = OrientationSuggestion{};
}

bool providerIsOllama(const std::string& provider) {
    return provider.empty() || provider == "ollama" || provider == "Ollama";
}

DescriptorResult runSmartTagLoop(int64_t modelId,
                                 const std::string& modelName,
                                 const std::string& thumbnailPath,
                                 const std::string& endpoint,
                                 const std::string& aiModel,
                                 LMStudioDescriptorService* service,
                                 std::function<bool(int)> applyOrientationCorrection,
                                 std::function<bool(ThumbnailView)> regenerateThumbnail,
                                 const char* resultEventName) {
    ThumbnailView currentView = ThumbnailView::Unknown;
    std::vector<ThumbnailView> triedViews;
    DescriptorResult result;
    bool orientationCorrectionTried = false;

    for (int attempt = 0; attempt < smart_tagging::kMaxPerpendicularRetries + 2; ++attempt) {
        triedViews.push_back(currentView);
        result = service->describe(thumbnailPath, endpoint, aiModel, currentView);
        appendManualTaggerResultLog(modelId, modelName, resultEventName, result);

        if (result.success && result.orientation.needsRotation &&
            result.orientation.rotateDegrees != 0 && !orientationCorrectionTried &&
            applyOrientationCorrection &&
            applyOrientationCorrection(result.orientation.rotateDegrees)) {
            orientationCorrectionTried = true;
            ThumbnailView correctedView = result.orientation.uprightView;
            if (correctedView == ThumbnailView::Unknown) {
                correctedView = currentView == ThumbnailView::Unknown
                                    ? ThumbnailView::Front
                                    : currentView;
            }
            appendManualTaggerLog(
                std::string("orientation_correction model_id=") + std::to_string(modelId) +
                " name=\"" + modelName +
                "\" rotate_degrees=" + std::to_string(result.orientation.rotateDegrees) +
                " corrected_view=" + smart_tagging::thumbnailViewName(correctedView));
            appendManualTaggerLog(
                std::string("thumbnail_request model_id=") + std::to_string(modelId) +
                " name=\"" + modelName + "\" requested_view=" +
                smart_tagging::thumbnailViewName(correctedView));
            if (!regenerateThumbnail(correctedView)) {
                result.success = false;
                result.error = "Failed to generate orientation-corrected thumbnail";
                appendManualTaggerResultLog(modelId, modelName, resultEventName, result);
                return result;
            }
            appendManualTaggerLog(
                std::string("thumbnail_complete model_id=") + std::to_string(modelId) +
                " name=\"" + modelName + "\" rendered_view=" +
                smart_tagging::thumbnailViewName(correctedView));
            currentView = correctedView;
            continue;
        }

        auto decision = smart_tagging::decideNextStep(
            result,
            triedViews,
            static_cast<int>(std::count_if(
                triedViews.begin(), triedViews.end(), smart_tagging::isPerpendicularView)));

        appendManualTaggerLog(
            std::string("manual_decision model_id=") + std::to_string(modelId) +
            " name=\"" + modelName + "\" attempt=" + std::to_string(attempt + 1) +
            " view=" + smart_tagging::thumbnailViewName(currentView) +
            " decision=" + smart_tagging::tagDecisionActionName(decision.action) +
            " next_view=" + smart_tagging::thumbnailViewName(decision.nextView));

        if (decision.action == smart_tagging::TagDecisionAction::Accept ||
            decision.action == smart_tagging::TagDecisionAction::Failed) {
            return result;
        }

        if (decision.action == smart_tagging::TagDecisionAction::Unclassifiable) {
            markManualUnclassifiable(result);
            return result;
        }

        appendManualTaggerLog(
            std::string("thumbnail_request model_id=") + std::to_string(modelId) +
            " name=\"" + modelName + "\" requested_view=" +
            smart_tagging::thumbnailViewName(decision.nextView));
        if (!regenerateThumbnail(decision.nextView)) {
            result.success = false;
            result.error = std::string("Failed to generate ") +
                           smart_tagging::thumbnailViewName(decision.nextView) + " thumbnail";
            appendManualTaggerResultLog(modelId, modelName, resultEventName, result);
            return result;
        }
        appendManualTaggerLog(
            std::string("thumbnail_complete model_id=") + std::to_string(modelId) +
            " name=\"" + modelName + "\" rendered_view=" +
            smart_tagging::thumbnailViewName(decision.nextView));
        currentView = decision.nextView;
    }

    markManualUnclassifiable(result);
    return result;
}

} // namespace

bool Application::prepareAiTagging(std::string& endpoint, std::string& model) {
    auto& config = Config::instance();
    model = config.getAiModel().empty() ? "llava:latest" : config.getAiModel();

    if (providerIsOllama(config.getAiProvider())) {
        if (!m_ollamaRuntime)
            m_ollamaRuntime = std::make_unique<OllamaRuntime>();

        OllamaRuntimeConfig runtimeConfig;
        runtimeConfig.model = model;
        runtimeConfig.port = static_cast<uint16_t>(config.getOllamaPrivatePort());
        runtimeConfig.manageProcess = true;
        runtimeConfig.autoPullMissingModel = true;
        m_ollamaRuntime->setConfig(runtimeConfig);

        auto status = m_ollamaRuntime->ensureReady();
        if (!status.ready) {
            log::warningf(
                "AI", "%s. Action: %s", status.reason.c_str(), status.actionCommand.c_str());
            ToastManager::instance().show(ToastType::Warning,
                                          "Local AI Not Ready",
                                          status.reason + "\nRun: " + status.actionCommand);
            return false;
        }
        if (status.modelDownloaded) {
            ToastManager::instance().show(ToastType::Success,
                                          "Local AI Ready",
                                          "Downloaded " + model + " for automatic tagging");
        }

        endpoint = status.endpoint;
        return true;
    }

    endpoint = config.getLMStudioEndpoint();
    if (endpoint.empty()) {
        ToastManager::instance().show(ToastType::Warning,
                                      "AI Tagging",
                                      "Local AI endpoint not configured");
        return false;
    }
    return true;
}

void Application::wireTagDialog() {
    auto* tagDlg = m_uiManager->tagImageDialog();
    if (!tagDlg)
        return;

    tagDlg->setOnRequest([this, tagDlg](int64_t modelId) {
        if (!m_descriptorService || !m_mainThreadQueue)
            return;
        std::string endpoint;
        std::string aiModel;
        if (!prepareAiTagging(endpoint, aiModel)) {
            log::warning("App", "Local AI endpoint not configured");
            DescriptorResult error;
            error.error = "Local AI is not ready";
            tagDlg->setResult(error);
            return;
        }
        auto record = m_libraryManager->getModel(modelId);
        if (!record || record->thumbnailPath.empty()) {
            DescriptorResult error;
            error.error = "Model has no thumbnail";
            tagDlg->setResult(error);
            return;
        }
        resetAiTagStateForRetag(m_libraryManager.get(), modelId);
        auto* service = m_descriptorService.get();
        auto* queue = m_mainThreadQueue.get();
        std::string thumbnailPath = record->thumbnailPath.string();
        std::string modelName = record->name;
        appendManualTaggerLog(
            "manual_request model_id=" + std::to_string(modelId) + " name=\"" + modelName +
            "\" view=unknown thumbnail=\"" + thumbnailPath + "\"");
        std::thread([this,
                     service,
                     queue,
                     tagDlg,
                     modelId,
                     modelName,
                     thumbnailPath,
                     endpoint,
                     aiModel]() {
            auto result = runSmartTagLoop(
                modelId,
                modelName,
                thumbnailPath,
                endpoint,
                aiModel,
                service,
                [this, modelId](int rotateDegrees) {
                    return applyAiOrientationCorrection(modelId, rotateDegrees);
                },
                [this, modelId](ThumbnailView view) {
                    return regenerateSmartTagThumbnail(modelId, view);
                },
                "manual_result");
            queue->enqueue([tagDlg, result]() { tagDlg->setResult(result); });
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
        log::infof("App",
                   "Tagged model %lld as: %s",
                   static_cast<long long>(modelId),
                   result.title.c_str());
    });
    m_uiManager->libraryPanel()->setOnTagImage(
        [this](const std::vector<int64_t>& modelIds) { handleTagImage(modelIds); });
}

void Application::handleTagImage(const std::vector<int64_t>& modelIds) {
    if (modelIds.empty())
        return;
    auto* tagDlg = m_uiManager->tagImageDialog();
    if (!tagDlg)
        return;

    if (modelIds.size() == 1) {
        auto record = m_libraryManager->getModel(modelIds[0]);
        if (!record)
            return;
        GLuint texture =
            m_uiManager->libraryPanel()->getThumbnailTextureForModel(modelIds[0]);
        tagDlg->open(*record, texture);
        return;
    }
    std::string endpoint;
    std::string aiModel;
    if (!prepareAiTagging(endpoint, aiModel)) {
        log::warning("App", "Local AI endpoint not configured");
        return;
    }
    auto* service = m_descriptorService.get();
    auto* libraryManager = m_libraryManager.get();
    auto* queue = m_mainThreadQueue.get();
    auto* libraryPanel = m_uiManager->libraryPanel();
    const size_t count = modelIds.size();
    for (int64_t modelId : modelIds) {
        auto record = m_libraryManager->getModel(modelId);
        if (!record || record->thumbnailPath.empty())
            continue;
        resetAiTagStateForRetag(m_libraryManager.get(), modelId);
        std::string thumbnailPath = record->thumbnailPath.string();
        std::string modelName = record->name;
        appendManualTaggerLog(
            "batch_request model_id=" + std::to_string(modelId) + " name=\"" + modelName +
            "\" view=unknown thumbnail=\"" + thumbnailPath + "\"");
        std::thread([this,
                     service,
                     libraryManager,
                     queue,
                     libraryPanel,
                     modelId,
                     modelName,
                     thumbnailPath,
                     endpoint,
                     aiModel]() {
            auto result = runSmartTagLoop(
                modelId,
                modelName,
                thumbnailPath,
                endpoint,
                aiModel,
                service,
                [this, modelId](int rotateDegrees) {
                    return applyAiOrientationCorrection(modelId, rotateDegrees);
                },
                [this, modelId](ThumbnailView view) {
                    return regenerateSmartTagThumbnail(modelId, view);
                },
                "batch_result");
            queue->enqueue([libraryManager, libraryPanel, modelId, modelName, result]() {
                if (result.success &&
                    result.status != TagClassificationStatus::Unclassifiable) {
                    persistTagResults(libraryManager, modelId, result);
                    libraryPanel->refresh();
                    libraryPanel->invalidateThumbnail(modelId);
                    ToastManager::instance().show(ToastType::Success, "Tagged", result.title);
                    log::infof(
                        "App", "Tagged %s as: %s", modelName.c_str(), result.title.c_str());
                } else {
                    libraryManager->updateTagStatus(
                        modelId,
                        result.status == TagClassificationStatus::Unclassifiable ? 4 : 3);
                    log::warningf("App",
                                  "Descriptor failed for %s: %s",
                                  modelName.c_str(),
                                  result.error.c_str());
                }
            });
        }).detach();
    }
    ToastManager::instance().show(ToastType::Info,
                                  "Tagging",
                                  "Classifying " + std::to_string(count) + " models...");
}

} // namespace dw
