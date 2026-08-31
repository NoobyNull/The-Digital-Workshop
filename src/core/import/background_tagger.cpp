#include "background_tagger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "../database/connection_pool.h"
#include "../database/model_repository.h"
#include "../library/library_manager.h"
#include "../loaders/loader_factory.h"
#include "../materials/lmstudio_descriptor_service.h"
#include "../paths/app_paths.h"
#include "../paths/path_resolver.h"
#include "../utils/log.h"

namespace dw {

namespace {

void markUnclassifiable(DescriptorResult& result) {
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

void appendTaggerLog(const std::string& line) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream out(paths::getDataDir() / "tagger.log", std::ios::app);
    if (!out.is_open())
        return;
    out << utcTimestamp() << ' ' << line << '\n';
}

const char* classificationStatusName(TagClassificationStatus status) {
    switch (status) {
    case TagClassificationStatus::FinalTag: return "final_tag";
    case TagClassificationStatus::RetryView: return "retry_view";
    case TagClassificationStatus::FallbackIsometric: return "fallback_isometric";
    case TagClassificationStatus::Unclassifiable: return "unclassifiable";
    }
    return "final_tag";
}

void logAttemptResult(const char* mode,
                      const ModelRecord& model,
                      int attempt,
                      ThumbnailView currentView,
                      const DescriptorResult& result,
                      const smart_tagging::TagDecision& decision) {
    const char* status = result.success ? "ok" : "error";
    const char* classification = result.success ? smart_tagging::tagDecisionActionName(decision.action)
                                                : "failed";
    log::infof("Tagger",
               "%s attempt model=%lld '%s' attempt=%d view=%s status=%s confidence=%.2f "
               "response_status=%s needsRetag=%s recommendedView=%s decision=%s reason='%s' "
               "orientation=%s/%d title='%s'",
               mode,
               static_cast<long long>(model.id),
               model.name.c_str(),
               attempt,
               smart_tagging::thumbnailViewName(currentView),
               status,
               static_cast<double>(result.confidence),
               classificationStatusName(result.status),
               result.needsRetag ? "true" : "false",
               smart_tagging::thumbnailViewName(result.recommendedView),
               classification,
               result.viewReason.c_str(),
               result.orientation.needsRotation ? "true" : "false",
               result.orientation.rotateDegrees,
               result.title.c_str());

    std::ostringstream line;
    line << mode << " model_id=" << model.id
         << " name=\"" << model.name << "\""
         << " attempt=" << attempt
         << " view=" << smart_tagging::thumbnailViewName(currentView)
         << " success=" << (result.success ? "true" : "false")
         << " confidence=" << result.confidence
         << " response_status=" << classificationStatusName(result.status)
         << " needs_retag=" << (result.needsRetag ? "true" : "false")
         << " recommended_view=" << smart_tagging::thumbnailViewName(result.recommendedView)
         << " decision=" << classification
         << " reason=\"" << result.viewReason << "\""
         << " orientation_needs_rotation="
         << (result.orientation.needsRotation ? "true" : "false")
         << " orientation_rotate_degrees=" << result.orientation.rotateDegrees
         << " orientation_upright_view="
         << smart_tagging::thumbnailViewName(result.orientation.uprightView)
         << " orientation_reason=\"" << result.orientation.reason << "\""
         << " title=\"" << result.title << "\""
         << " error=\"" << result.error << "\"";
    appendTaggerLog(line.str());
}

} // namespace

BackgroundTagger::BackgroundTagger(ConnectionPool& pool,
                                   LibraryManager* libraryMgr,
                                   LMStudioDescriptorService* descriptorSvc)
    : m_pool(pool), m_libraryMgr(libraryMgr), m_descriptorSvc(descriptorSvc) {}

BackgroundTagger::~BackgroundTagger() {
    stop();
    join();
}

void BackgroundTagger::start(const std::string& endpoint,
                             const std::string& model,
                             BackgroundTaggerMode mode) {
    if (m_progress.active.load())
        return;

    // Join any previous thread
    join();

    m_endpoint = endpoint;
    m_model = model;
    m_mode = mode;
    m_stopRequested.store(false);
    m_progress.totalUntagged.store(0);
    m_progress.completed.store(0);
    m_progress.failed.store(0);
    m_progress.active.store(true);
    {
        std::lock_guard<std::mutex> lock(m_progress.nameMutex);
        m_progress.currentModel[0] = '\0';
        m_progress.statusMessage[0] = '\0';
    }

    m_thread = std::thread([this]() { workerLoop(); });
}

void BackgroundTagger::stop() {
    m_stopRequested.store(true);
}

void BackgroundTagger::join() {
    if (m_thread.joinable())
        m_thread.join();
}

bool BackgroundTagger::isActive() const {
    return m_progress.active.load();
}

const TaggerProgress& BackgroundTagger::progress() const {
    return m_progress;
}

void BackgroundTagger::setThumbnailViewCallback(ThumbnailViewCallback callback) {
    m_thumbnailViewCallback = std::move(callback);
}

void BackgroundTagger::setCurrentModel(const std::string& modelName) {
    std::lock_guard<std::mutex> lock(m_progress.nameMutex);
    std::strncpy(m_progress.currentModel,
                 modelName.c_str(),
                 sizeof(m_progress.currentModel) - 1);
    m_progress.currentModel[sizeof(m_progress.currentModel) - 1] = '\0';
}

void BackgroundTagger::setStatusMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_progress.nameMutex);
    std::strncpy(m_progress.statusMessage,
                 message.c_str(),
                 sizeof(m_progress.statusMessage) - 1);
    m_progress.statusMessage[sizeof(m_progress.statusMessage) - 1] = '\0';
}

ThumbnailView BackgroundTagger::initialViewForModel(const ModelRecord& model) const {
    if (model.orientYaw && std::fabs(*model.orientYaw - 180.0f) < 45.0f) {
        return ThumbnailView::Back;
    }
    return ThumbnailView::Front;
}

bool BackgroundTagger::applyOrientationCorrection(ModelRepository& repo,
                                                  const ModelRecord& model,
                                                  int clockwiseDegrees) const {
    auto loadResult =
        LoaderFactory::load(PathResolver::resolve(model.filePath, PathCategory::Support));
    if (!loadResult) {
        log::warningf("Tagger",
                      "Failed to load model %lld for AI orientation correction: %s",
                      static_cast<long long>(model.id),
                      loadResult.error.c_str());
        return false;
    }

    f32 orientYaw = model.orientYaw.value_or(0.0f);
    Mat4 baseMatrix(1.0f);
    if (model.orientMatrix) {
        loadResult.mesh->applyStoredOrient(*model.orientMatrix);
        baseMatrix = loadResult.mesh->getOrientMatrix();
    } else {
        orientYaw = loadResult.mesh->autoOrient();
        baseMatrix = loadResult.mesh->getOrientMatrix();
    }

    Mat4 corrected =
        smart_tagging::orientationCorrectionMatrix(clockwiseDegrees) * baseMatrix;
    bool ok = repo.updateOrient(model.id, orientYaw, corrected);
    log::infof("Tagger",
               "AI orientation correction model=%lld clockwise=%d result=%s",
               static_cast<long long>(model.id),
               clockwiseDegrees,
               ok ? "ok" : "failed");
    return ok;
}

void BackgroundTagger::persistSuccessfulResult(ModelRepository& repo,
                                               int64_t modelId,
                                               const DescriptorResult& result) {
    m_libraryMgr->updateDescriptor(modelId,
                                   result.title,
                                   result.description,
                                   result.hoverNarrative);

    auto existing = m_libraryMgr->getModel(modelId);
    if (existing) {
        auto tags = existing->tags;
        for (const auto& kw : result.keywords)
            tags.push_back(kw);
        for (const auto& assoc : result.associations)
            tags.push_back(assoc);
        m_libraryMgr->updateTags(modelId, tags);
    }

    if (!result.categories.empty())
        m_libraryMgr->resolveAndAssignCategories(modelId, result.categories);

    repo.updateTagStatus(modelId, 2); // tagged
}

DescriptorResult BackgroundTagger::runImportTagAttempt(const ModelRecord& model) {
    ThumbnailView currentView = initialViewForModel(model);
    setStatusMessage(std::string("classifying ") +
                     smart_tagging::thumbnailViewName(currentView));
    DescriptorResult result =
        m_descriptorSvc->describe(model.thumbnailPath.string(), m_endpoint, m_model, currentView);
    if (!result.success) {
        return result;
    }

    std::vector<ThumbnailView> triedViews{currentView};
    auto decision = smart_tagging::decideNextStep(
        result, triedViews, smart_tagging::kMaxPerpendicularRetries);

    logAttemptResult("import", model, 1, currentView, result, decision);

    if (decision.action != smart_tagging::TagDecisionAction::Accept) {
        markUnclassifiable(result);
    }

    return result;
}

DescriptorResult BackgroundTagger::runSmartTagAttempt(ModelRepository& repo,
                                                      const ModelRecord& model) {
    ThumbnailView currentView = initialViewForModel(model);
    std::vector<ThumbnailView> triedViews;
    DescriptorResult result;
    bool orientationCorrectionTried = false;

    for (int attempt = 0; attempt < smart_tagging::kMaxPerpendicularRetries + 2; ++attempt) {
        if (m_stopRequested.load()) {
            result.success = false;
            result.error = "Tagging stopped";
            return result;
        }

        auto latest = repo.findById(model.id);
        if (!latest || latest->thumbnailPath.empty()) {
            result.success = false;
            result.error = "Model has no thumbnail";
            return result;
        }

        triedViews.push_back(currentView);
        setStatusMessage(std::string("classifying ") +
                         smart_tagging::thumbnailViewName(currentView));
        result =
            m_descriptorSvc->describe(latest->thumbnailPath.string(), m_endpoint, m_model, currentView);

        if (result.success && result.orientation.needsRotation &&
            result.orientation.rotateDegrees != 0 && !orientationCorrectionTried &&
            m_thumbnailViewCallback && applyOrientationCorrection(
                                           repo, *latest, result.orientation.rotateDegrees)) {
            orientationCorrectionTried = true;
            ThumbnailView correctedView = result.orientation.uprightView;
            if (correctedView == ThumbnailView::Unknown)
                correctedView = currentView == ThumbnailView::Unknown ? ThumbnailView::Front
                                                                      : currentView;
            appendTaggerLog(std::string("orientation_correction model_id=") +
                            std::to_string(model.id) + " name=\"" + model.name +
                            "\" rotate_degrees=" +
                            std::to_string(result.orientation.rotateDegrees) +
                            " corrected_view=" +
                            smart_tagging::thumbnailViewName(correctedView));
            setStatusMessage(std::string("thumbnailing ") +
                             smart_tagging::thumbnailViewName(correctedView));
            appendTaggerLog(std::string("thumbnail_request model_id=") +
                            std::to_string(model.id) + " name=\"" + model.name +
                            "\" requested_view=" +
                            smart_tagging::thumbnailViewName(correctedView));
            if (!m_thumbnailViewCallback(model.id, correctedView)) {
                result.success = false;
                result.error = "Failed to generate orientation-corrected thumbnail";
                return result;
            }
            appendTaggerLog(std::string("thumbnail_complete model_id=") +
                            std::to_string(model.id) + " name=\"" + model.name +
                            "\" rendered_view=" +
                            smart_tagging::thumbnailViewName(correctedView));
            currentView = correctedView;
            continue;
        }

        auto decision = smart_tagging::decideNextStep(
            result,
            triedViews,
            static_cast<int>(std::count_if(triedViews.begin(),
                                           triedViews.end(),
                                           smart_tagging::isPerpendicularView)));

        logAttemptResult("smart", model, attempt + 1, currentView, result, decision);

        if (decision.action == smart_tagging::TagDecisionAction::Accept ||
            decision.action == smart_tagging::TagDecisionAction::Failed) {
            return result;
        }

        if (decision.action == smart_tagging::TagDecisionAction::Unclassifiable) {
            markUnclassifiable(result);
            return result;
        }

        if (!m_thumbnailViewCallback) {
            markUnclassifiable(result);
            result.viewReason = "thumbnail view regeneration unavailable";
            return result;
        }

        setStatusMessage(std::string("thumbnailing ") +
                         smart_tagging::thumbnailViewName(decision.nextView));
        log::infof("Tagger",
                   "Regenerating smart-tag thumbnail for model=%lld '%s' requestedView=%s",
                   static_cast<long long>(model.id),
                   model.name.c_str(),
                   smart_tagging::thumbnailViewName(decision.nextView));
        appendTaggerLog(std::string("thumbnail_request model_id=") +
                        std::to_string(model.id) + " name=\"" + model.name +
                        "\" requested_view=" +
                        smart_tagging::thumbnailViewName(decision.nextView));
        if (!m_thumbnailViewCallback(model.id, decision.nextView)) {
            result.success = false;
            result.error = std::string("Failed to generate ") +
                           smart_tagging::thumbnailViewName(decision.nextView) + " thumbnail";
            return result;
        }
        appendTaggerLog(std::string("thumbnail_complete model_id=") +
                        std::to_string(model.id) + " name=\"" + model.name +
                        "\" rendered_view=" +
                        smart_tagging::thumbnailViewName(decision.nextView));

        currentView = decision.nextView;
    }

    markUnclassifiable(result);
    return result;
}

void BackgroundTagger::workerLoop() {
    ScopedConnection conn(m_pool);
    ModelRepository repo(*conn);

    int recovered = repo.recoverInterruptedTagStatuses();
    if (recovered > 0) {
        log::warningf("Tagger",
                      "Recovered %d interrupted tag status value(s)",
                      recovered);
    }

    // Count total AI-tag candidates, including failed/manual-review rows that
    // the user explicitly starts again from the menu.
    int total = repo.countAiTagCandidates();
    m_progress.totalUntagged.store(total);
    log::infof("Tagger", "Starting background tagging: %d candidate models", total);

    i64 lastAttemptedId = 0;
    while (!m_stopRequested.load()) {
        auto model = repo.findNextAiTagCandidate(lastAttemptedId);
        if (!model) {
            log::info("Tagger", "No more AI tag candidates");
            break;
        }
        lastAttemptedId = model->id;

        setCurrentModel(model->name);
        int originalStatus = model->tagStatus;

        // Mark in-progress
        repo.updateTagStatus(model->id, 1);

        // Check stop before expensive API call
        if (m_stopRequested.load()) {
            repo.updateTagStatus(model->id, originalStatus);
            break;
        }

        DescriptorResult result =
            m_mode == BackgroundTaggerMode::SmartRetag
                ? runSmartTagAttempt(repo, *model)
                : runImportTagAttempt(*model);

        // Check stop after API call
        if (m_stopRequested.load()) {
            repo.updateTagStatus(model->id, originalStatus);
            break;
        }

        if (result.success) {
            if (result.status == TagClassificationStatus::Unclassifiable) {
                repo.updateTagStatus(model->id, 4); // unclassifiable/manual review
                m_progress.failed.fetch_add(1);
                log::warningf("Tagger",
                              "Could not classify '%s': %s",
                              model->name.c_str(),
                              result.viewReason.empty() ? "insufficient visual identity"
                                                        : result.viewReason.c_str());
            } else {
                persistSuccessfulResult(repo, model->id, result);
                m_progress.completed.fetch_add(1);
                log::infof("Tagger",
                           "Tagged '%s' as: %s",
                           model->name.c_str(),
                           result.title.c_str());
            }
        } else {
            repo.updateTagStatus(model->id, 3); // failed
            m_progress.failed.fetch_add(1);
            log::warningf("Tagger",
                          "Failed to tag '%s': %s",
                          model->name.c_str(),
                          result.error.c_str());
        }
    }

    m_progress.active.store(false);
    log::infof("Tagger",
               "Background tagging finished: %d tagged, %d failed",
               m_progress.completed.load(),
               m_progress.failed.load());
}

} // namespace dw
