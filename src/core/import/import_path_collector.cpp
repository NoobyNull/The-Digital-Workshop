#include "core/import/import_path_collector.h"

#include <algorithm>
#include <system_error>

#include "core/utils/file_utils.h"

namespace dw::import_paths {
namespace {

bool isSupportedModelExtension(const std::string& extension) {
    return extension == "stl" || extension == "obj" || extension == "3mf";
}

Path absoluteImportPath(const Path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }

    std::error_code ec;
    Path absolute = fs::absolute(path, ec);
    return ec ? path : absolute;
}

bool reportProgress(const ScanProgressCallback& callback, const ScanProgress& progress) {
    return !callback || callback(progress);
}

bool collectFromDirectory(const Path& directory,
                          std::vector<Path>& outPaths,
                          ScanProgress& progress,
                          const ScanProgressCallback& progressCallback) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return true;
    }

    ++progress.directoriesVisited;
    progress.currentPath = directory;
    if (!reportProgress(progressCallback, progress)) {
        return false;
    }

    std::vector<Path> files;
    std::vector<Path> directories;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) {
            return true;
        }

        const Path path = entry.path();
        if (entry.is_regular_file(ec)) {
            files.push_back(path);
        } else if (!ec && entry.is_directory(ec)) {
            directories.push_back(path);
        }
        ec.clear();
    }

    std::sort(files.begin(), files.end());
    std::sort(directories.begin(), directories.end());

    for (const auto& file : files) {
        ++progress.filesVisited;
        progress.currentPath = file;
        const bool supported = isSupportedModelFile(file);
        if (supported) {
            ++progress.supportedFilesFound;
        }
        if (!reportProgress(progressCallback, progress)) {
            return false;
        }
        if (supported) {
            outPaths.push_back(absoluteImportPath(file));
        }
    }

    for (const auto& childDirectory : directories) {
        if (!collectFromDirectory(childDirectory, outPaths, progress, progressCallback)) {
            return false;
        }
    }

    return true;
}

} // namespace

bool isSupportedModelFile(const Path& path) {
    return isSupportedModelExtension(file::getExtension(path));
}

std::vector<Path> collectSupportedModelFiles(const Path& root,
                                             ScanProgressCallback progressCallback) {
    std::vector<Path> paths;
    ScanProgress progress;

    std::error_code ec;
    if (fs::is_regular_file(root, ec)) {
        ++progress.filesVisited;
        progress.currentPath = root;
        if (isSupportedModelFile(root)) {
            ++progress.supportedFilesFound;
        }
        if (!reportProgress(progressCallback, progress)) {
            return {};
        }
        if (isSupportedModelFile(root)) {
            paths.push_back(absoluteImportPath(root));
        }
        return paths;
    }

    collectFromDirectory(root, paths, progress, progressCallback);
    return paths;
}

} // namespace dw::import_paths
