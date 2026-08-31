#pragma once

#include "../types.h"

namespace dw {

class Config;

// Category of user-visible content directory
enum class PathCategory {
    Models,
    Projects,
    Materials,
    GCode,
    Support, // CAS blob store and internal support files
};

// Single choke point for all path read/write operations.
// Stored DB locations are relative to their category root, absolute local
// filesystem paths, or durable network URLs.
namespace PathResolver {

// Get the current configured root directory for a category.
Path categoryRoot(PathCategory cat);

// Resolve a stored DB location to a usable filesystem path. Durable network
// URLs and stale desktop bridge paths are materialized through the platform
// network-location adapter. Ordinary absolute paths pass through, while
// relative paths are resolved beneath their category root.
Path resolve(const Path& storedPath, PathCategory cat);

// Convert an input location to its persistent representation. Network bridge
// paths become durable URLs; local paths under the category root become
// relative; other local absolute paths remain unchanged.
Path makeStorable(const Path& absolutePath, PathCategory cat);

// Return the stable user-facing form of a stored location. Network locations
// are represented by their durable URL; local locations are fully resolved.
Path durableLocation(const Path& storedPath, PathCategory cat);

// Return the stable parent location suitable for the platform file manager.
// A network item produces a network URL parent so the desktop can reconnect it
// instead of receiving a stale session-specific bridge path.
Path fileManagerParent(const Path& storedPath, PathCategory cat);

} // namespace PathResolver
} // namespace dw
