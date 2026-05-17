#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "smart_tagger.h"

namespace dw {

class ConnectionPool;
class LibraryManager;
class LMStudioDescriptorService;
class ModelRepository;
struct ModelRecord;

enum class BackgroundTaggerMode {
    ImportSinglePass,
    SmartRetag,
};

struct TaggerProgress {
    std::atomic<int> totalUntagged{0};
    std::atomic<int> completed{0};
    std::atomic<int> failed{0};
    std::atomic<bool> active{false};
    std::mutex nameMutex;
    char currentModel[256]{};
    char statusMessage[256]{};
};

class BackgroundTagger {
  public:
    BackgroundTagger(ConnectionPool& pool,
                     LibraryManager* libraryMgr,
                     LMStudioDescriptorService* descriptorSvc);
    ~BackgroundTagger();

    using ThumbnailViewCallback = std::function<bool(int64_t modelId, ThumbnailView view)>;

    void start(const std::string& endpoint,
               const std::string& model,
               BackgroundTaggerMode mode = BackgroundTaggerMode::ImportSinglePass);
    void stop();
    void join();
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] const TaggerProgress& progress() const;
    void setThumbnailViewCallback(ThumbnailViewCallback callback);

  private:
    void workerLoop();
    void persistSuccessfulResult(ModelRepository& repo,
                                 int64_t modelId,
                                 const DescriptorResult& result);
    [[nodiscard]] ThumbnailView initialViewForModel(const ModelRecord& model) const;
    [[nodiscard]] bool applyOrientationCorrection(ModelRepository& repo,
                                                  const ModelRecord& model,
                                                  int clockwiseDegrees) const;
    [[nodiscard]] DescriptorResult runImportTagAttempt(const ModelRecord& model);
    [[nodiscard]] DescriptorResult runSmartTagAttempt(ModelRepository& repo,
                                                      const ModelRecord& model);
    void setCurrentModel(const std::string& modelName);
    void setStatusMessage(const std::string& message);

    ConnectionPool& m_pool;
    LibraryManager* m_libraryMgr;
    LMStudioDescriptorService* m_descriptorSvc;
    ThumbnailViewCallback m_thumbnailViewCallback;

    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::string m_endpoint;
    std::string m_model;
    BackgroundTaggerMode m_mode = BackgroundTaggerMode::ImportSinglePass;
    TaggerProgress m_progress;
};

} // namespace dw
