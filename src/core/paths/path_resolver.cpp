#include "path_resolver.h"

#include "../config/config.h"
#include "app_paths.h"

#include <string>

namespace dw {
namespace PathResolver {

namespace {

Path remapStaleKioFuseSmbPath(const Path& path) {
    std::string raw = path.string();
    constexpr std::string_view marker = "/smb/synology.local/STL/";
    auto pos = raw.find(marker);
    if (pos == std::string::npos || raw.rfind("/run/user/", 0) != 0) {
        return path;
    }

    std::string relative = raw.substr(pos + marker.size());
    return Path("/mnt/synology-stl") / relative;
}

} // namespace

Path categoryRoot(PathCategory cat) {
    auto& cfg = Config::instance();
    switch (cat) {
    case PathCategory::Models:
        return cfg.getModelsDir();
    case PathCategory::Projects:
        return cfg.getProjectsDir();
    case PathCategory::Materials:
        return cfg.getMaterialsDir();
    case PathCategory::GCode:
        return cfg.getGCodeDir();
    case PathCategory::Support:
        return cfg.getSupportDir();
    }
    return cfg.getSupportDir();
}

Path resolve(const Path& storedPath, PathCategory cat) {
    if (storedPath.empty()) {
        return storedPath;
    }
    if (storedPath.is_absolute()) {
        if (!std::filesystem::exists(storedPath)) {
            return remapStaleKioFuseSmbPath(storedPath);
        }
        return storedPath;
    }
    Path resolved = categoryRoot(cat) / storedPath;

    // Materials live in two directories: user dir and bundled dir.
    // Check user dir first so user overrides take priority.
    if (cat == PathCategory::Materials && !std::filesystem::exists(resolved)) {
        Path bundled = paths::getBundledMaterialsDir() / storedPath;
        if (std::filesystem::exists(bundled)) {
            return bundled;
        }
    }
    return resolved;
}

Path makeStorable(const Path& absolutePath, PathCategory cat) {
    if (absolutePath.empty() || !absolutePath.is_absolute()) {
        return absolutePath;
    }
    Path root = categoryRoot(cat);
    if (root.empty()) {
        return absolutePath;
    }

    // Use lexically_relative for cross-platform path comparison
    Path relative = absolutePath.lexically_relative(root);
    // lexically_relative returns "" on failure or starts with ".." if outside root
    if (relative.empty() || *relative.begin() == "..") {
        return absolutePath;
    }
    return relative;
}

} // namespace PathResolver
} // namespace dw
