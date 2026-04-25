#include "file_handler.h"

#include <filesystem>
#include <system_error>

#include "../config/config.h"
#include "../paths/app_paths.h"
#include "../utils/log.h"

namespace dw {

Path FileHandler::handleImportedFile(const Path& source,
                                     FileHandlingMode mode,
                                     const Path& libraryRoot,
                                     std::string& error) {
    error.clear();

    // Leave in place - just return the original path
    if (mode == FileHandlingMode::LeaveInPlace) {
        return source;
    }

    // Ensure library directory exists
    if (!ensureLibraryDir(libraryRoot)) {
        error = "Failed to create library directory: " + libraryRoot.string();
        return Path();
    }

    // Generate unique destination path
    Path dest = uniqueDestination(libraryRoot, source.filename());
    if (dest.empty()) {
        error = "Failed to generate unique destination path";
        return Path();
    }

    try {
        if (mode == FileHandlingMode::CopyToLibrary) {
            // Copy file to library
            std::filesystem::copy(source, dest, std::filesystem::copy_options::overwrite_existing);
            log::infof("FileHandler",
                       "Copied file: %s -> %s",
                       source.string().c_str(),
                       dest.string().c_str());
            return dest;
        } else if (mode == FileHandlingMode::MoveToLibrary) {
            // Try atomic rename first (works if same filesystem)
            try {
                std::filesystem::rename(source, dest);
                log::infof("FileHandler",
                           "Moved file: %s -> %s",
                           source.string().c_str(),
                           dest.string().c_str());
                return dest;
            } catch (const std::filesystem::filesystem_error&) {
                // Cross-filesystem move - fallback to copy + delete
                log::debugf("FileHandler",
                            "Rename failed, using copy+delete for: %s",
                            source.string().c_str());

                // Copy file
                std::filesystem::copy(source,
                                      dest,
                                      std::filesystem::copy_options::overwrite_existing);

                // Verify sizes match
                auto sourceSize = std::filesystem::file_size(source);
                auto destSize = std::filesystem::file_size(dest);
                if (sourceSize != destSize) {
                    error = "File size mismatch after copy (source: " + std::to_string(sourceSize) +
                            ", dest: " + std::to_string(destSize) + ")";
                    std::filesystem::remove(dest); // Clean up partial copy
                    return Path();
                }

                // Delete source
                std::filesystem::remove(source);
                log::infof("FileHandler",
                           "Moved file (cross-filesystem): %s -> %s",
                           source.string().c_str(),
                           dest.string().c_str());
                return dest;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        error = "Filesystem error: " + std::string(e.what());
        return Path();
    } catch (const std::exception& e) {
        error = "Unexpected error: " + std::string(e.what());
        return Path();
    }

    error = "Invalid file handling mode";
    return Path();
}

bool FileHandler::ensureLibraryDir(const Path& libraryRoot) {
    try {
        std::filesystem::create_directories(libraryRoot);
        return std::filesystem::is_directory(libraryRoot);
    } catch (const std::filesystem::filesystem_error& e) {
        log::errorf("FileHandler", "Failed to create library directory: %s", e.what());
        return false;
    }
}

Path FileHandler::defaultLibraryDir() {
    return Config::instance().getModelsDir();
}

Path FileHandler::uniqueDestination(const Path& dir, const Path& filename) {
    // Try the filename as-is first
    Path candidate = dir / filename;
    if (!std::filesystem::exists(candidate)) {
        return candidate;
    }

    // Extract stem and extension
    Path stem = filename.stem();
    Path extension = filename.extension();

    // Try appending numbers: filename_1.ext, filename_2.ext, etc.
    for (int i = 1; i <= 10000; ++i) {
        std::string newFilename = stem.string() + "_" + std::to_string(i) + extension.string();
        candidate = dir / newFilename;
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    // Exhausted attempts
    log::error("FileHandler", "Failed to generate unique destination after 10000 attempts");
    return Path();
}

} // namespace dw
