#pragma once

#include <functional>
#include <string>

#include "../types.h"

namespace dw {
namespace network_location {

// Mount a durable network URL and return the local filesystem path exposed by
// the desktop's network filesystem bridge. Returning std::nullopt means the
// URL could not be mounted.
using Mounter = std::function<Result<Path>(const std::string&)>;

// Return a session-independent URL for a KIO-FUSE path or an already durable
// network URL. Local filesystem paths and unsafe network-looking values do not
// have a durable URL. An explicit runtime root is a deterministic test seam;
// production callers should leave it empty.
Result<std::string> durableUrl(const Path& path, const Path& runtimeRoot = {});

// True when the value is a KIO-FUSE path or a durable non-file URL.
bool isNetworkLocation(const Path& path, const Path& runtimeRoot = {});

// True for anything shaped like a URL or a KIO-FUSE location, including unsafe
// values rejected by durableUrl(). Callers use this to fail closed instead of
// treating invalid network input as an ordinary relative filesystem path.
bool isNetworkLocationCandidate(const Path& path, const Path& runtimeRoot = {});

// Return a URI-aware parent for a valid network location, or filesystem parent
// for a local path. Unsafe network-looking values return an empty path.
Path parentLocation(const Path& path, const Path& runtimeRoot = {});

// Resolve a network location to a usable local path. Existing paths pass
// through unchanged. Stale KIO-FUSE paths are first rebased onto a live sibling
// mount, then mounted through org.kde.KIOFuse when rebasing is not possible.
// Supplying a mounter makes the external mount operation deterministic in tests.
Path resolve(const Path& path, const Mounter& mounter = Mounter{}, const Path& runtimeRoot = {});

} // namespace network_location
} // namespace dw
