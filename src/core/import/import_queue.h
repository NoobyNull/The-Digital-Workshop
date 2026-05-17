#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../config/config.h"
#include "../database/connection_pool.h"
#include "../threading/thread_pool.h"
#include "import_task.h"

namespace dw {

// Forward declarations
class ImportLog;
class LibraryManager;
class StorageManager;

// Import parsing expands source files into meshes, so import batches need a
// tighter memory-aware worker cap than generic CPU jobs.
size_t calculateImportThreadCountForBatch(const std::vector<Path>& paths, ParallelismTier tier);

class ImportQueue {
  public:
    explicit ImportQueue(ConnectionPool& pool,
                         LibraryManager* libraryManager = nullptr,
                         StorageManager* storageManager = nullptr);
    ~ImportQueue();

    // Enqueue files for import (called from main thread)
    void enqueue(const std::vector<Path>& paths);
    void enqueue(const std::vector<Path>& paths, FileHandlingMode mode);
    void enqueue(const Path& path);

    // Re-enqueue selected duplicates (skips duplicate check)
    void enqueueForReimport(const std::vector<DuplicateRecord>& duplicates);

    // Cancel all pending imports
    void cancel();

    // Check if queue is actively processing
    bool isActive() const;

    // Get progress (thread-safe, lock-free reads)
    const ImportProgress& progress() const;

    // Poll for completed tasks that need main-thread work (thumbnail gen)
    // Returns tasks ready for GL operations. Caller takes ownership.
    std::vector<ImportTask> pollCompleted();

    // Mark a task as fully done after thumbnail generation
    void taskFinished(i64 modelId);

    // Set callback for when a task finishes completely (after thumbnail)
    using ImportCallback = std::function<void(const ImportTask& task)>;
    void setOnComplete(ImportCallback callback);

    // Get batch summary (thread-safe read)
    const ImportBatchSummary& batchSummary() const;

    // Set callback for when batch completes
    using SummaryCallback = std::function<void(const ImportBatchSummary&)>;
    void setOnBatchComplete(SummaryCallback callback);

    // Import log for resume/revert support
    void setImportLog(ImportLog* log);

    // Tag queueing mode
    void setQueueForTagging(bool enabled);
    bool queueForTagging() const;

  private:
    void processTask(ImportTask task); // Note: takes by value for move into lambda
    void enqueueInternal(const std::vector<Path>& paths); // Shared impl for both enqueue overloads

    // --- Pipeline stage helpers (called from processTask on worker thread) ---
    // Each stage returns false on failure (task already marked failed + progress updated).

    struct TaskContext; // Forward-declared, defined in .cpp

    void failTask(ImportTask& task, const std::string& error);
    void checkBatchComplete();
    void enqueueBatch(const std::vector<Path>& paths,
                      FileHandlingMode mode,
                      bool queueForTagging,
                      const char* logLabel);

    bool stageReadFile(ImportTask& task);
    bool stageValidate(ImportTask& task);
    void stageComputeHash(ImportTask& task);
    bool stageCheckDuplicate(ImportTask& task, TaskContext& ctx);
    bool stageParse(ImportTask& task);
    bool stageInsertGCode(ImportTask& task, TaskContext& ctx, u64 fileSize);
    bool stageInsertMesh(ImportTask& task, TaskContext& ctx, u64 fileSize);
    void stageHandleFile(ImportTask& task, TaskContext& ctx);
    void stageFinalize(ImportTask& task);

    ConnectionPool& m_pool;
    LibraryManager* m_libraryManager; // Optional, for auto-detect
    StorageManager* m_storageManager; // Optional, for CAS blob storage

    // Thread management
    std::unique_ptr<ThreadPool> m_threadPool; // Lazily created
    std::mutex m_mutex;
    std::condition_variable m_completedCv;
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_cancelRequested{false};

    // Task queues (protected by m_mutex)
    std::vector<ImportTask> m_completed; // Ready for main-thread thumbnail

    // Batch tracking
    mutable std::mutex m_batchStateMutex;
    mutable std::mutex m_summaryMutex;
    ImportBatchSummary m_batchSummary;
    std::atomic<int> m_remainingTasks{0};

    // Per-batch file handling mode (set by overloaded enqueue, or from Config)
    FileHandlingMode m_batchMode = FileHandlingMode::MoveToLibrary;

    ImportProgress m_progress;
    ImportCallback m_onComplete;
    SummaryCallback m_onBatchComplete;

    ImportLog* m_importLog = nullptr;
    bool m_queueForTagging = false;
};

} // namespace dw
