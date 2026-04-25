#include "import_queue.h"

#include "../config/config.h"
#include "../database/connection_pool.h"
#include "../database/gcode_repository.h"
#include "../database/model_repository.h"
#include "../import/file_handler.h"
#include "../library/library_manager.h"
#include "../loaders/gcode_loader.h"
#include "../loaders/loader_factory.h"
#include "../mesh/hash.h"
#include "../paths/path_resolver.h"
#include "../storage/storage_manager.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "file_validator.h"
#include "import_log.h"

namespace dw {

ImportQueue::ImportQueue(ConnectionPool& pool,
                         LibraryManager* libraryManager,
                         StorageManager* storageManager)
    : m_pool(pool), m_libraryManager(libraryManager), m_storageManager(storageManager) {}

ImportQueue::~ImportQueue() {
    m_shutdown.store(true);
    if (m_threadPool) {
        m_threadPool->shutdown();
    }
}

void ImportQueue::enqueue(const std::vector<Path>& paths, FileHandlingMode mode) {
    if (paths.empty()) {
        return;
    }

    m_batchMode = mode;
    enqueueBatch(paths, mode, m_queueForTagging, "Starting batch import");
}

void ImportQueue::enqueue(const std::vector<Path>& paths) {
    if (paths.empty()) {
        return;
    }

    // Use global config as default batch mode when called without explicit mode
    m_batchMode = Config::instance().getFileHandlingMode();
    enqueueBatch(paths, m_batchMode, m_queueForTagging, "Starting batch import");
}

void ImportQueue::enqueueInternal(const std::vector<Path>& paths) {
    enqueueBatch(paths, m_batchMode, m_queueForTagging, "Starting batch import");
}

void ImportQueue::enqueueBatch(const std::vector<Path>& paths,
                               FileHandlingMode mode,
                               bool queueForTagging,
                               const char* logLabel) {
    if (paths.empty())
        return;

    std::lock_guard<std::mutex> stateLock(m_batchStateMutex);
    const bool extendingActiveBatch = m_progress.active.load();

    {
        std::lock_guard<std::mutex> summaryLock(m_summaryMutex);
        if (!extendingActiveBatch) {
            m_batchSummary = ImportBatchSummary{};
        }
        m_batchSummary.totalFiles += static_cast<int>(paths.size());
    }

    // Get thread count from config
    auto tier = Config::instance().getParallelismTier();
    size_t threadCount = calculateThreadCount(tier);

    // Lazy-create or recreate ThreadPool with current thread count
    if (!m_threadPool || m_threadPool->isIdle()) {
        m_threadPool = std::make_unique<ThreadPool>(threadCount);
    }

    if (extendingActiveBatch) {
        log::infof(
            "Import", "Adding %zu file(s) to active import batch", paths.size());
        m_progress.totalFiles.fetch_add(static_cast<int>(paths.size()));
        m_remainingTasks.fetch_add(static_cast<int>(paths.size()));
    } else {
        log::infof("Import", "%s: %zu files, %zu workers", logLabel, paths.size(), threadCount);

        m_progress.reset();
        m_progress.totalFiles.store(static_cast<int>(paths.size()));
        m_progress.active.store(true);
        m_cancelRequested.store(false);

        m_remainingTasks.store(static_cast<int>(paths.size()));
    }

    // Enqueue each file as a task
    for (const auto& path : paths) {
        ImportTask task;
        task.sourcePath = path;
        task.extension = file::getExtension(path);
        task.importType = importTypeFromExtension(task.extension);
        task.fileHandlingMode = mode;
        task.queueForTagging = queueForTagging;

        // Enqueue to thread pool — shared_ptr bridges the move-only ImportTask
        // across std::function's copy-constructible requirement
        auto taskPtr = std::make_shared<ImportTask>(std::move(task));
        m_threadPool->enqueue([this, taskPtr]() { processTask(std::move(*taskPtr)); });
    }
}

void ImportQueue::enqueue(const Path& path) {
    enqueue(std::vector<Path>{path});
}

void ImportQueue::cancel() {
    m_cancelRequested.store(true);
    log::info("Import", "Cancelled by user");
}

bool ImportQueue::isActive() const {
    return m_progress.active.load();
}

const ImportProgress& ImportQueue::progress() const {
    return m_progress;
}

std::vector<ImportTask> ImportQueue::pollCompleted() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ImportTask> result = std::move(m_completed);
    m_completed.clear();
    return result;
}

void ImportQueue::taskFinished(i64 modelId) {
    // Find and invoke callback if set
    if (m_onComplete) {
        // Create a minimal task with the modelId for the callback
        ImportTask task;
        task.modelId = modelId;
        task.stage = ImportStage::Done;
        m_onComplete(task);
    }
}

void ImportQueue::setOnComplete(ImportCallback callback) {
    m_onComplete = std::move(callback);
}

const ImportBatchSummary& ImportQueue::batchSummary() const {
    std::lock_guard<std::mutex> lock(m_summaryMutex);
    return m_batchSummary;
}

void ImportQueue::setOnBatchComplete(SummaryCallback callback) {
    m_onBatchComplete = std::move(callback);
}

void ImportQueue::setImportLog(ImportLog* log) {
    m_importLog = log;
}

void ImportQueue::setQueueForTagging(bool enabled) {
    m_queueForTagging = enabled;
}

bool ImportQueue::queueForTagging() const {
    return m_queueForTagging;
}

// --- Shared failure/completion helpers (runs on ThreadPool worker) ---

// Per-task context holding the pooled DB connection and repositories.
// Constructed once at the top of processTask and threaded through stages.
struct ImportQueue::TaskContext {
    ScopedConnection conn;
    ModelRepository modelRepo;
    GCodeRepository gcodeRepo;

    explicit TaskContext(ConnectionPool& pool)
        : conn(pool), modelRepo(*conn), gcodeRepo(*conn) {}
};

void ImportQueue::failTask(ImportTask& task, const std::string& error) {
    task.stage = ImportStage::Failed;
    task.error = error;

    if (!error.empty() && error != "Cancelled")
        log::errorf("Import", "%s", error.c_str());

    {
        std::lock_guard<std::mutex> lock(m_summaryMutex);
        m_batchSummary.failedCount++;
        m_batchSummary.errors.push_back({file::getStem(task.sourcePath), task.error});
    }

    m_progress.failedFiles.fetch_add(1);
    m_progress.completedFiles.fetch_add(1);
    checkBatchComplete();
}

void ImportQueue::checkBatchComplete() {
    ImportBatchSummary summary;
    bool completed = false;
    {
        std::lock_guard<std::mutex> stateLock(m_batchStateMutex);
        completed = (m_remainingTasks.fetch_sub(1) == 1);
        if (completed) {
            m_progress.active.store(false);
            std::lock_guard<std::mutex> summaryLock(m_summaryMutex);
            summary = m_batchSummary;
        }
    }

    if (completed) {
        log::infof("Import",
                   "Batch complete: %d successful, %d failed, %d duplicates",
                   summary.successCount,
                   summary.failedCount,
                   summary.duplicateCount);

        if (m_onBatchComplete) {
            m_onBatchComplete(summary);
        }
    }
}

// --- Pipeline stage functions (runs on ThreadPool worker) ---

bool ImportQueue::stageReadFile(ImportTask& task) {
    task.stage = ImportStage::Reading;
    m_progress.currentStage.store(ImportStage::Reading);

    auto fileData = file::readBinary(task.sourcePath);
    if (!fileData) {
        failTask(task, "Failed to read file: " + task.sourcePath.string());
        return false;
    }
    task.fileData = std::move(*fileData);
    return true;
}

bool ImportQueue::stageValidate(ImportTask& task) {
    task.stage = ImportStage::Validating;
    m_progress.currentStage.store(ImportStage::Validating);

    auto result = file_validator::validate(task.fileData, task.extension);
    if (!result.valid) {
        failTask(task, "Validation failed: " + result.reason);
        return false;
    }
    return true;
}

void ImportQueue::stageComputeHash(ImportTask& task) {
    task.stage = ImportStage::Hashing;
    m_progress.currentStage.store(ImportStage::Hashing);

    task.fileHash = hash::computeBuffer(task.fileData);
}

bool ImportQueue::stageCheckDuplicate(ImportTask& task, TaskContext& ctx) {
    task.stage = ImportStage::CheckingDuplicate;
    m_progress.currentStage.store(ImportStage::CheckingDuplicate);

    if (task.skipDuplicateCheck)
        return true;

    bool isDuplicate = false;
    std::string duplicateName;

    if (task.importType == ImportType::GCode) {
        auto existing = ctx.gcodeRepo.findByHash(task.fileHash);
        if (existing) {
            isDuplicate = true;
            duplicateName = existing->name;
        }
    } else {
        auto existing = ctx.modelRepo.findByHash(task.fileHash);
        if (existing) {
            isDuplicate = true;
            duplicateName = existing->name;
        }
    }

    if (!isDuplicate)
        return true;

    // Duplicate found — record it and exit pipeline
    task.isDuplicate = true;

    DuplicateRecord dup;
    dup.sourcePath = task.sourcePath;
    dup.extension = task.extension;
    dup.importType = task.importType;
    dup.fileHash = task.fileHash;
    dup.existingName = duplicateName;

    {
        std::lock_guard<std::mutex> lock(m_summaryMutex);
        m_batchSummary.duplicateCount++;
        m_batchSummary.duplicates.push_back(std::move(dup));
    }

    if (m_importLog)
        m_importLog->appendDup(task.sourcePath, task.fileHash);

    log::warningf("Import",
                  "Duplicate found: '%s' (of '%s')",
                  task.sourcePath.filename().string().c_str(),
                  duplicateName.c_str());

    // Duplicates are pending user decision, not failures — just mark completed
    m_progress.completedFiles.fetch_add(1);
    checkBatchComplete();
    return false;
}

bool ImportQueue::stageParse(ImportTask& task) {
    task.stage = ImportStage::Parsing;
    m_progress.currentStage.store(ImportStage::Parsing);

    if (task.importType == ImportType::GCode) {
        GCodeLoader gcodeLoader;
        auto result = gcodeLoader.loadFromBuffer(task.fileData);
        if (!result) {
            failTask(task, "Parse failed: " + result.error);
            return false;
        }

        task.mesh = result.mesh;
        task.gcodeMetadata = std::make_unique<GCodeMetadata>(gcodeLoader.lastMetadata());
    } else {
        auto loadResult = LoaderFactory::loadFromBuffer(task.fileData, task.extension);
        if (!loadResult) {
            failTask(task, "Parse failed: " + loadResult.error);
            return false;
        }
        task.mesh = loadResult.mesh;
    }

    return true;
}

bool ImportQueue::stageInsertGCode(ImportTask& task, TaskContext& ctx, u64 fileSize) {
    auto mode = task.fileHandlingMode;

    GCodeRecord record;
    record.hash = task.fileHash;
    record.name = file::getStem(task.sourcePath);

    if (m_storageManager && mode != FileHandlingMode::LeaveInPlace) {
        record.filePath = PathResolver::makeStorable(
            m_storageManager->blobPath(task.fileHash, task.extension), PathCategory::Support);
    } else {
        record.filePath = PathResolver::makeStorable(task.sourcePath, PathCategory::GCode);
    }
    record.fileSize = fileSize;

    // Populate metadata from extracted data
    if (task.gcodeMetadata) {
        record.boundsMin = task.gcodeMetadata->boundsMin;
        record.boundsMax = task.gcodeMetadata->boundsMax;
        record.totalDistance = task.gcodeMetadata->totalDistance;
        record.estimatedTime = task.gcodeMetadata->estimatedTime;
        record.feedRates = task.gcodeMetadata->feedRates;
        record.toolNumbers = task.gcodeMetadata->toolNumbers;
    }

    auto gcodeId = ctx.gcodeRepo.insert(record);
    if (!gcodeId) {
        task.gcodeMetadata.reset();
        failTask(task,
                 "Failed to insert into database for '" +
                     task.sourcePath.filename().string() + "'");
        return false;
    }

    task.gcodeId = *gcodeId;

    log::infof(
        "Import", "G-code '%s' inserted (id=%lld)", record.name.c_str(),
        static_cast<long long>(*gcodeId));

    // Auto-detect model association if LibraryManager available
    if (m_libraryManager) {
        auto matchedModelId = m_libraryManager->autoDetectModelMatch(record.name);
        if (matchedModelId) {
            auto modelRecord = ctx.modelRepo.findById(*matchedModelId);
            if (modelRecord) {
                auto groups = m_libraryManager->getOperationGroups(*matchedModelId);
                i64 groupId = 0;

                if (groups.empty()) {
                    auto newGroupId =
                        m_libraryManager->createOperationGroup(*matchedModelId, "Imported", 0);
                    if (newGroupId) {
                        groupId = *newGroupId;
                        log::infof("Import",
                                   "Created 'Imported' group for model '%s'",
                                   modelRecord->name.c_str());
                    }
                } else {
                    groupId = groups[0].id;
                }

                if (groupId > 0) {
                    if (m_libraryManager->addGCodeToGroup(groupId, *gcodeId, 0)) {
                        log::infof("Import",
                                   "Auto-associated '%s' with model '%s'",
                                   record.name.c_str(),
                                   modelRecord->name.c_str());
                    }
                }
            }
        } else {
            log::infof("Import",
                       "No model match for '%s', imported as standalone",
                       record.name.c_str());
        }
    }

    return true;
}

bool ImportQueue::stageInsertMesh(ImportTask& task, TaskContext& ctx, u64 fileSize) {
    auto mode = task.fileHandlingMode;

    // Precompute autoOrient on the worker thread (pure CPU, no GL)
    if (Config::instance().getAutoOrient() && task.mesh && task.mesh->isValid() &&
        task.mesh->validateGeometry()) {
        f32 orientYaw = task.mesh->autoOrient();
        task.record.orientYaw = orientYaw;
        task.record.orientMatrix = task.mesh->getOrientMatrix();
    }

    ModelRecord record;
    record.hash = task.fileHash;
    record.name = file::getStem(task.sourcePath);

    if (m_storageManager && mode != FileHandlingMode::LeaveInPlace) {
        record.filePath = PathResolver::makeStorable(
            m_storageManager->blobPath(task.fileHash, task.extension), PathCategory::Support);
    } else {
        record.filePath = PathResolver::makeStorable(task.sourcePath, PathCategory::Models);
    }
    record.fileFormat = task.extension;
    record.fileSize = fileSize;
    record.vertexCount = task.mesh->vertexCount();
    record.triangleCount = task.mesh->triangleCount();

    const auto& bounds = task.mesh->bounds();
    record.boundsMin = bounds.min;
    record.boundsMax = bounds.max;

    record.orientYaw = task.record.orientYaw;
    record.orientMatrix = task.record.orientMatrix;

    auto modelId = ctx.modelRepo.insert(record);
    if (!modelId) {
        failTask(task,
                 "Failed to insert into database for '" +
                     task.sourcePath.filename().string() + "'");
        return false;
    }

    task.modelId = *modelId;
    task.record = record;
    task.record.id = *modelId;

    if (task.queueForTagging)
        ctx.modelRepo.updateTagStatus(*modelId, 1);

    log::infof("Import",
               "Mesh '%s' inserted (id=%lld)",
               record.name.c_str(),
               static_cast<long long>(*modelId));

    return true;
}

void ImportQueue::stageHandleFile(ImportTask& task, TaskContext& ctx) {
    auto mode = task.fileHandlingMode;

    if (m_storageManager && mode != FileHandlingMode::LeaveInPlace) {
        // CAS blob store path
        std::string storageError;
        Path storedPath;
        if (mode == FileHandlingMode::CopyToLibrary) {
            storedPath = m_storageManager->storeFile(
                task.sourcePath, task.fileHash, task.extension, storageError);
        } else if (mode == FileHandlingMode::MoveToLibrary) {
            storedPath = m_storageManager->moveFile(
                task.sourcePath, task.fileHash, task.extension, storageError);
        }

        if (storedPath.empty()) {
            // Blob store failed -- roll back the DB insert
            log::errorf("Import",
                        "Blob store failed for '%s': %s",
                        file::getStem(task.sourcePath).c_str(),
                        storageError.c_str());

            if (task.importType == ImportType::GCode) {
                ctx.gcodeRepo.remove(task.gcodeId);
            } else {
                ctx.modelRepo.remove(task.modelId);
            }

            failTask(task, "Blob store failed: " + storageError);
            return;
        }

        log::infof("Import",
                   "Stored in blob: %s -> %s",
                   task.sourcePath.filename().string().c_str(),
                   storedPath.filename().string().c_str());

    } else if (mode != FileHandlingMode::LeaveInPlace) {
        // Fallback: legacy FileHandler when no StorageManager
        auto libraryDir = Config::instance().getModelsDir();

        std::string error;
        Path handledPath =
            FileHandler::handleImportedFile(task.sourcePath, mode, libraryDir, error);

        if (handledPath.empty()) {
            log::warningf("Import",
                          "File handling failed for '%s': %s",
                          file::getStem(task.sourcePath).c_str(),
                          error.c_str());
        } else {
            if (task.importType == ImportType::GCode) {
                auto record = ctx.gcodeRepo.findById(task.gcodeId);
                if (record) {
                    record->filePath =
                        PathResolver::makeStorable(handledPath, PathCategory::GCode);
                    ctx.gcodeRepo.update(*record);
                }
            } else {
                task.record.filePath =
                    PathResolver::makeStorable(handledPath, PathCategory::Models);
                ctx.modelRepo.update(task.record);
            }

            log::infof("Import",
                       "File handled: %s -> %s",
                       task.sourcePath.filename().string().c_str(),
                       handledPath.filename().string().c_str());
        }
    }
}

void ImportQueue::stageFinalize(ImportTask& task) {
    // Log successful import
    if (m_importLog)
        m_importLog->appendDone(task.sourcePath, task.fileHash);

    // Hand off to main thread for thumbnail generation
    task.stage = ImportStage::WaitingForThumbnail;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_completed.push_back(std::move(task));
    }

    m_progress.completedFiles.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(m_summaryMutex);
        m_batchSummary.successCount++;
    }

    checkBatchComplete();
}

// --- Worker task processing (runs on ThreadPool worker) ---

void ImportQueue::processTask(ImportTask task) {
    // Check for cancellation at start
    if (m_cancelRequested.load()) {
        failTask(task, "Cancelled");
        return;
    }

    // Acquire a pooled connection and create repositories for this task
    TaskContext ctx(m_pool);

    // Update progress with current file name (thread-safe)
    m_progress.setCurrentFileName(file::getStem(task.sourcePath));

    // Stage 1: Read file from disk
    if (!stageReadFile(task))
        return;

    // Stage 2: Validate file content (magic bytes, structure, size)
    if (!stageValidate(task))
        return;

    // Stage 3: Compute content hash
    stageComputeHash(task);

    // Stage 4: Check for duplicates
    if (!stageCheckDuplicate(task, ctx))
        return;

    // Stage 5: Parse file contents (mesh or G-code)
    if (!stageParse(task))
        return;

    // Capture file size before releasing buffer
    u64 actualFileSize = static_cast<u64>(task.fileData.size());

    // Release file data memory now that parsing is done
    task.fileData.clear();
    task.fileData.shrink_to_fit();

    // Stage 5: Insert record into database (type-specific)
    task.stage = ImportStage::Inserting;
    m_progress.currentStage.store(ImportStage::Inserting);

    if (task.importType == ImportType::GCode) {
        if (!stageInsertGCode(task, ctx, actualFileSize))
            return;
    } else {
        if (!stageInsertMesh(task, ctx, actualFileSize))
            return;
    }

    // Stage 5.5: Apply file handling mode (copy/move/leave-in-place)
    stageHandleFile(task, ctx);
    if (task.stage == ImportStage::Failed)
        return; // stageHandleFile already called failTask on blob store failure

    // Stage 6: Finalize — queue for thumbnail and update counters
    stageFinalize(task);
}

void ImportQueue::enqueueForReimport(const std::vector<DuplicateRecord>& duplicates) {
    if (duplicates.empty())
        return;

    log::infof("Import", "Re-importing %zu selected duplicate(s)", duplicates.size());

    std::lock_guard<std::mutex> stateLock(m_batchStateMutex);
    const bool extendingActiveBatch = m_progress.active.load();
    {
        std::lock_guard<std::mutex> summaryLock(m_summaryMutex);
        if (!extendingActiveBatch) {
            m_batchSummary = ImportBatchSummary{};
        }
        m_batchSummary.totalFiles += static_cast<int>(duplicates.size());
    }

    auto tier = Config::instance().getParallelismTier();
    size_t threadCount = calculateThreadCount(tier);

    if (!m_threadPool || m_threadPool->isIdle()) {
        m_threadPool = std::make_unique<ThreadPool>(threadCount);
    }

    if (extendingActiveBatch) {
        m_progress.totalFiles.fetch_add(static_cast<int>(duplicates.size()));
        m_remainingTasks.fetch_add(static_cast<int>(duplicates.size()));
    } else {
        m_progress.reset();
        m_progress.totalFiles.store(static_cast<int>(duplicates.size()));
        m_progress.active.store(true);
        m_cancelRequested.store(false);
        m_remainingTasks.store(static_cast<int>(duplicates.size()));
    }

    for (const auto& dup : duplicates) {
        ImportTask task;
        task.sourcePath = dup.sourcePath;
        task.extension = dup.extension;
        task.importType = dup.importType;
        task.skipDuplicateCheck = true;
        task.fileHandlingMode = m_batchMode;
        task.queueForTagging = m_queueForTagging;

        auto taskPtr = std::make_shared<ImportTask>(std::move(task));
        m_threadPool->enqueue([this, taskPtr]() { processTask(std::move(*taskPtr)); });
    }
}

} // namespace dw
