#include "thumbnail_sidecar.h"

#include "../utils/string_utils.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace dw {
namespace {

struct SidecarCandidate {
    Path path;
    int priority = 0;
    std::string sortKey;
};

std::string normalizeExtension(std::string extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }
    return str::toLower(extension);
}

bool isThumbnailStemForModel(const std::string& modelStem,
                             const std::string& imageStem,
                             int& priority) {
    if (imageStem == modelStem) {
        priority = 0;
        return true;
    }

    static constexpr const char* kSuffixes[] = {"-thumb",
                                                "_thumb",
                                                "-thumbnail",
                                                "_thumbnail",
                                                "-preview",
                                                "_preview",
                                                "-render",
                                                "_render",
                                                "-image",
                                                "_image"};
    for (const char* suffix : kSuffixes) {
        if (imageStem == modelStem + suffix) {
            priority = 1;
            return true;
        }
    }

    return false;
}

} // namespace

bool isSupportedThumbnailImageExtension(std::string extension) {
    extension = normalizeExtension(std::move(extension));
    return extension == "png" || extension == "jpg" || extension == "jpeg" ||
           extension == "bmp" || extension == "tga";
}

std::optional<Path> findSidecarThumbnailForImport(const Path& modelPath) {
    Path directory = modelPath.parent_path();
    if (directory.empty()) {
        directory = ".";
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return std::nullopt;
    }

    const std::string modelStem = str::toLower(modelPath.stem().string());
    if (modelStem.empty()) {
        return std::nullopt;
    }

    std::vector<SidecarCandidate> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }

        const Path imagePath = entry.path();
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        if (!isSupportedThumbnailImageExtension(imagePath.extension().string())) {
            continue;
        }

        int priority = 0;
        const std::string imageStem = str::toLower(imagePath.stem().string());
        if (!isThumbnailStemForModel(modelStem, imageStem, priority)) {
            continue;
        }

        candidates.push_back({imagePath, priority, str::toLower(imagePath.filename().string())});
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const SidecarCandidate& lhs, const SidecarCandidate& rhs) {
                  if (lhs.priority != rhs.priority) {
                      return lhs.priority < rhs.priority;
                  }
                  return lhs.sortKey < rhs.sortKey;
              });

    return candidates.front().path;
}

} // namespace dw
