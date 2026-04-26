#pragma once

#include <functional>
#include <vector>

#include "core/types.h"

namespace dw::import_paths {

struct ScanProgress {
    int directoriesVisited = 0;
    int filesVisited = 0;
    int supportedFilesFound = 0;
    Path currentPath;
};

using ScanProgressCallback = std::function<bool(const ScanProgress&)>;

bool isSupportedModelFile(const Path& path);
std::vector<Path> collectSupportedModelFiles(const Path& root,
                                             ScanProgressCallback progressCallback = {});

} // namespace dw::import_paths
