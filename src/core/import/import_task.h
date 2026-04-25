#pragma once

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../database/model_repository.h"
#include "../loaders/gcode_loader.h"
#include "../mesh/mesh.h"
#include "../types.h"

namespace dw {

enum class FileHandlingMode : int;

// Import type enum - determines processing path
enum class ImportType { Mesh, GCode };

// Helper function to determine import type from extension
inline ImportType importTypeFromExtension(const std::string& ext) {
    if (ext == "gcode" || ext == "nc" || ext == "ngc" || ext == "tap") {
        return ImportType::GCode;
    }
    return ImportType::Mesh;
}

// Stage of an individual import task
enum class ImportStage {
    Pending,
    Reading,
    Validating,
    Hashing,
    CheckingDuplicate,
    Parsing,
    Inserting,
    WaitingForThumbnail, // Handed off to main thread for GL work
    Done,
    Failed
};

// Info about a duplicate detected during import (lightweight, no file data)
struct DuplicateRecord {
    Path sourcePath;
    std::string extension;
    ImportType importType = ImportType::Mesh;
    std::string fileHash;
    std::string existingName; // name of the file it duplicates
};

// Batch summary for import operations
struct ImportBatchSummary {
    int totalFiles = 0;
    int successCount = 0;
    int failedCount = 0;
    int duplicateCount = 0;
    std::vector<DuplicateRecord> duplicates;                 // Duplicates pending user review
    std::vector<std::pair<std::string, std::string>> errors; // (filename, error message)

    bool hasIssues() const { return failedCount > 0 || duplicateCount > 0; }
};

// One import job — tracks a single file through the pipeline
struct ImportTask {
    // Input
    Path sourcePath;
    std::string extension;
    ImportType importType = ImportType::Mesh;
    FileHandlingMode fileHandlingMode{};
    bool queueForTagging = false;

    // Pipeline data (populated as stages complete)
    ByteBuffer fileData;
    std::string fileHash;
    MeshPtr mesh;
    ModelRecord record;
    i64 modelId = 0;

    // G-code specific data (only populated when importType == GCode)
    std::unique_ptr<GCodeMetadata> gcodeMetadata;
    i64 gcodeId = 0;

    // State
    ImportStage stage = ImportStage::Pending;
    std::string error;
    bool isDuplicate = false;
    bool skipDuplicateCheck = false;
};

// Thread-safe progress readable from UI thread
struct ImportProgress {
    std::atomic<int> totalFiles{0};
    std::atomic<int> completedFiles{0};
    std::atomic<int> failedFiles{0};
    std::atomic<bool> active{false};

    // Current file info — protected by fileNameMutex
    mutable std::mutex fileNameMutex;
    char currentFileName[256]{};
    std::atomic<ImportStage> currentStage{ImportStage::Pending};

    void reset() {
        totalFiles.store(0);
        completedFiles.store(0);
        failedFiles.store(0);
        active.store(false);
        {
            std::lock_guard<std::mutex> lock(fileNameMutex);
            currentFileName[0] = '\0';
        }
        currentStage.store(ImportStage::Pending);
    }

    void setCurrentFileName(const std::string& name) {
        std::lock_guard<std::mutex> lock(fileNameMutex);
        std::strncpy(currentFileName, name.c_str(), sizeof(currentFileName) - 1);
        currentFileName[sizeof(currentFileName) - 1] = '\0';
    }

    std::string getCurrentFileName() const {
        std::lock_guard<std::mutex> lock(fileNameMutex);
        return std::string(currentFileName);
    }

    int percentComplete() const {
        int total = totalFiles.load();
        if (total == 0)
            return 0;
        return (completedFiles.load() * 100) / total;
    }
};

// Human-readable stage name for UI
inline const char* importStageName(ImportStage stage) {
    switch (stage) {
    case ImportStage::Pending:
        return "Queued";
    case ImportStage::Reading:
        return "Reading file";
    case ImportStage::Validating:
        return "Validating";
    case ImportStage::Hashing:
        return "Computing hash";
    case ImportStage::CheckingDuplicate:
        return "Checking duplicates";
    case ImportStage::Parsing:
        return "Parsing mesh";
    case ImportStage::Inserting:
        return "Saving to library";
    case ImportStage::WaitingForThumbnail:
        return "Generating thumbnail";
    case ImportStage::Done:
        return "Done";
    case ImportStage::Failed:
        return "Failed";
    }
    return "Unknown";
}

} // namespace dw
