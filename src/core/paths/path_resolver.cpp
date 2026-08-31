#include "path_resolver.h"

#include "../config/config.h"
#include "app_paths.h"
#include "network_location.h"

namespace dw {
namespace PathResolver {

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

    if (network_location::isNetworkLocationCandidate(storedPath)) {
        return network_location::isNetworkLocation(storedPath)
                   ? network_location::resolve(storedPath)
                   : Path{};
    }

    if (storedPath.is_absolute()) {
        return storedPath;
    }

    Path resolved = network_location::resolve(categoryRoot(cat) / storedPath);

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
    if (network_location::isNetworkLocationCandidate(absolutePath)) {
        if (auto durableUrl = network_location::durableUrl(absolutePath))
            return Path(*durableUrl);
        return {};
    }

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

Path durableLocation(const Path& storedPath, PathCategory cat) {
    if (storedPath.empty()) {
        return storedPath;
    }

    if (network_location::isNetworkLocationCandidate(storedPath)) {
        if (auto durableUrl = network_location::durableUrl(storedPath))
            return Path(*durableUrl);
        return {};
    }

    Path resolved = resolve(storedPath, cat);
    if (auto durableUrl = network_location::durableUrl(resolved)) {
        return Path(*durableUrl);
    }
    return resolved;
}

Path fileManagerParent(const Path& storedPath, PathCategory cat) {
    return network_location::parentLocation(durableLocation(storedPath, cat));
}

} // namespace PathResolver
} // namespace dw
