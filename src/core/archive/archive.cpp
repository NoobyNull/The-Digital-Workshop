#include "archive.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

#include "../utils/file_utils.h"
#include "../utils/log.h"

namespace dw {

// Simple archive format without external dependencies
// Format: Header + File entries
// Each entry: path_len(4) + path + size(8) + data

namespace {

constexpr uint32_t MAGIC = 0x44575000; // "DWP\0"
constexpr uint32_t VERSION = 1;

struct ArchiveHeader {
    uint32_t magic = MAGIC;
    uint32_t version = VERSION;
    uint32_t entryCount = 0;
    uint32_t reserved = 0;
};

bool writeU32(std::ostream& out, uint32_t value) {
    return out.write(reinterpret_cast<const char*>(&value), sizeof(value)).good();
}

bool writeU64(std::ostream& out, uint64_t value) {
    return out.write(reinterpret_cast<const char*>(&value), sizeof(value)).good();
}

bool readU32(std::istream& in, uint32_t& value) {
    return !in.read(reinterpret_cast<char*>(&value), sizeof(value)).fail();
}

bool readU64(std::istream& in, uint64_t& value) {
    return !in.read(reinterpret_cast<char*>(&value), sizeof(value)).fail();
}

bool fitsStreamSize(uint64_t value) {
    return value <= static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max());
}

bool fitsSizeT(uint64_t value) {
    return value <= static_cast<uint64_t>(std::numeric_limits<size_t>::max());
}

std::vector<std::string> collectFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    return files;
}

std::string makeRelativePath(const std::string& basePath, const std::string& fullPath) {
    std::error_code ec;
    auto rel = fs::relative(fs::path(fullPath), fs::path(basePath), ec);
    if (ec) {
        return fullPath;
    }
    return rel.generic_string();
}

} // namespace

ArchiveResult ProjectArchive::create(const std::string& archivePath,
                                     const std::string& projectDir) {
    if (!file::isDirectory(projectDir)) {
        return ArchiveResult::fail("Project directory does not exist: " + projectDir);
    }

    // Collect all files
    auto files = collectFiles(projectDir);
    if (files.empty()) {
        return ArchiveResult::fail("No files to archive");
    }

    // Create output file
    std::ofstream out(archivePath, std::ios::binary);
    if (!out) {
        return ArchiveResult::fail("Failed to create archive file: " + archivePath);
    }

    // Write header
    ArchiveHeader header;
    header.entryCount = static_cast<uint32_t>(files.size());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    std::vector<std::string> archivedFiles;

    // Write each file
    for (const auto& filePath : files) {
        std::string relativePath = makeRelativePath(projectDir, filePath);

        // Read file content
        auto content = file::readBinary(filePath);
        if (!content) {
            log::warningf("Archive", "Failed to read file: %s", filePath.c_str());
            continue;
        }

        // Write path
        uint32_t pathLen = static_cast<uint32_t>(relativePath.length());
        if (!writeU32(out, pathLen)) {
            return ArchiveResult::fail("Failed to write path length");
        }
        out.write(relativePath.c_str(), pathLen);

        // Write content
        uint64_t contentSize = content->size();
        if (!writeU64(out, contentSize)) {
            return ArchiveResult::fail("Failed to write content size");
        }
        if (!fitsStreamSize(contentSize)) {
            return ArchiveResult::fail("File too large to write: " + relativePath);
        }
        out.write(reinterpret_cast<const char*>(content->data()),
                  static_cast<std::streamsize>(contentSize));

        archivedFiles.push_back(relativePath);
    }

    out.close();

    if (!out) {
        return ArchiveResult::fail("Failed to finalize archive");
    }

    log::infof("Archive", "Created with %zu files: %s", archivedFiles.size(), archivePath.c_str());

    return ArchiveResult::ok(std::move(archivedFiles));
}

ArchiveResult ProjectArchive::extract(const std::string& archivePath,
                                      const std::string& outputDir) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) {
        return ArchiveResult::fail("Failed to open archive: " + archivePath);
    }

    // Read header
    ArchiveHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.magic != MAGIC) {
        return ArchiveResult::fail("Invalid archive format");
    }

    if (header.version > VERSION) {
        return ArchiveResult::fail("Unsupported archive version");
    }

    // Create output directory
    if (!file::createDirectories(outputDir)) {
        return ArchiveResult::fail("Failed to create output directory: " + outputDir);
    }

    std::vector<std::string> extractedFiles;

    // Extract each file
    for (uint32_t i = 0; i < header.entryCount; ++i) {
        // Read path
        uint32_t pathLen = 0;
        if (!readU32(in, pathLen) || pathLen > 4096) {
            return ArchiveResult::fail("Failed to read path length");
        }

        std::string relativePath(pathLen, '\0');
        in.read(relativePath.data(), pathLen);

        // Security check: prevent path traversal
        if (relativePath.find("..") != std::string::npos) {
            return ArchiveResult::fail("Security error: path traversal detected");
        }

        // Read content size
        uint64_t contentSize = 0;
        if (!readU64(in, contentSize)) {
            return ArchiveResult::fail("Failed to read content size");
        }

        // Read content
        if (!fitsSizeT(contentSize) || !fitsStreamSize(contentSize)) {
            return ArchiveResult::fail("Archived file too large: " + relativePath);
        }
        std::vector<uint8_t> content(static_cast<size_t>(contentSize));
        in.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(contentSize));

        // Create output path
        std::string outputPath = outputDir + "/" + relativePath;

        // Create parent directories
        Path parentDir = file::getParent(outputPath);
        if (!parentDir.empty()) {
            if (!file::createDirectories(parentDir)) {
                return ArchiveResult::fail("Failed to create directory: " + parentDir.string());
            }
        }

        // Write file
        if (!file::writeBinary(outputPath, content)) {
            return ArchiveResult::fail("Failed to write file: " + outputPath);
        }

        extractedFiles.push_back(relativePath);
    }

    log::infof("Archive", "Extracted %zu files to: %s", extractedFiles.size(), outputDir.c_str());

    return ArchiveResult::ok(std::move(extractedFiles));
}

std::vector<ArchiveEntry> ProjectArchive::list(const std::string& archivePath) {
    std::vector<ArchiveEntry> entries;

    std::ifstream in(archivePath, std::ios::binary);
    if (!in) {
        return entries;
    }

    // Read header
    ArchiveHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.magic != MAGIC) {
        return entries;
    }

    // Read entries
    for (uint32_t i = 0; i < header.entryCount; ++i) {
        ArchiveEntry entry;

        // Read path
        uint32_t pathLen = 0;
        if (!readU32(in, pathLen) || pathLen > 4096) {
            break;
        }

        entry.path.resize(pathLen);
        in.read(entry.path.data(), pathLen);

        // Read content size
        if (!readU64(in, entry.uncompressedSize)) {
            break;
        }
        entry.compressedSize = entry.uncompressedSize; // No compression

        // Skip content
        if (!fitsStreamSize(entry.uncompressedSize)) {
            break;
        }
        in.seekg(static_cast<std::streamoff>(entry.uncompressedSize), std::ios::cur);

        entries.push_back(std::move(entry));
    }

    return entries;
}

bool ProjectArchive::isValidArchive(const std::string& archivePath) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) {
        return false;
    }

    ArchiveHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));

    return header.magic == MAGIC && header.version <= VERSION;
}

} // namespace dw
