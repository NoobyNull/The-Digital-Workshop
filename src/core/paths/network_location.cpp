#include "network_location.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __linux__
#include <dbus/dbus.h>
#include <unistd.h>
#endif

namespace dw {
namespace network_location {

namespace {

constexpr std::string_view kMountPrefix = "kio-fuse-";

struct ParsedUrl {
    std::string canonical;
    std::string scheme;
    std::string authority;
    std::vector<std::string> segments;
};

struct ParsedKioFusePath {
    Path runtimeRoot;
    Path mountRoot;
    Path relativePath;
    std::string scheme;
    std::string authority;
    std::vector<std::string> remoteSegments;
};

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isAsciiAlpha(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool isAsciiDigit(char value) {
    return value >= '0' && value <= '9';
}

bool isAsciiHex(char value) {
    return isAsciiDigit(value) || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');
}

unsigned char hexValue(char value) {
    if (isAsciiDigit(value))
        return static_cast<unsigned char>(value - '0');
    if (value >= 'A' && value <= 'F')
        return static_cast<unsigned char>(value - 'A' + 10);
    return static_cast<unsigned char>(value - 'a' + 10);
}

bool isUnreserved(char value) {
    return isAsciiAlpha(value) || isAsciiDigit(value) || value == '-' || value == '.' ||
           value == '_' || value == '~';
}

char lowerAsciiChar(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

char upperAsciiHex(char value) {
    return value >= 'a' && value <= 'f' ? static_cast<char>(value - 'a' + 'A') : value;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), lowerAsciiChar);
    return value;
}

bool isAllowedScheme(std::string_view scheme) {
    constexpr std::string_view allowed[] = {
        "smb", "sftp", "fish", "ftp", "ftps", "nfs", "webdav", "webdavs"};
    return std::find(std::begin(allowed), std::end(allowed), scheme) != std::end(allowed);
}

bool isValidMountName(std::string_view name) {
    if (!startsWith(name, kMountPrefix) || name.size() == kMountPrefix.size())
        return false;
    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(kMountPrefix.size()),
                       name.end(),
                       [](char value) { return isAsciiAlpha(value) || isAsciiDigit(value); });
}

Path normalizedRuntimeRoot(const Path& overrideRoot) {
    if (!overrideRoot.empty())
        return overrideRoot.is_absolute() ? overrideRoot.lexically_normal() : Path{};

#ifdef __linux__
    if (const char* configured = std::getenv("XDG_RUNTIME_DIR")) {
        Path root(configured);
        if (!root.empty() && root.is_absolute())
            return root.lexically_normal();
    }
    return Path("/run/user") / std::to_string(static_cast<unsigned long long>(getuid()));
#else
    return {};
#endif
}

bool hasUrlMarker(const Path& path) {
    return path.string().find("://") != std::string::npos;
}

bool isKioFuseShaped(const Path& path, const Path& runtimeRoot) {
    if (path.empty() || !path.is_absolute() || runtimeRoot.empty())
        return false;

    const Path relative = path.lexically_relative(runtimeRoot);
    if (relative.empty() || relative.is_absolute())
        return false;
    auto first = relative.begin();
    if (first == relative.end())
        return false;
    const std::string mountName = first->string();
    return startsWith(mountName, kMountPrefix);
}

Result<std::string> canonicalizeUserInfo(std::string_view userInfo) {
    if (userInfo.empty())
        return std::nullopt;

    std::string canonical;
    for (usize index = 0; index < userInfo.size(); ++index) {
        const char value = userInfo[index];
        if (value == ':')
            return std::nullopt;
        if (value == '%') {
            if (index + 2 >= userInfo.size() || !isAsciiHex(userInfo[index + 1]) ||
                !isAsciiHex(userInfo[index + 2])) {
                return std::nullopt;
            }
            const auto decoded = static_cast<unsigned char>((hexValue(userInfo[index + 1]) << 4) |
                                                            hexValue(userInfo[index + 2]));
            if (decoded == ':' || decoded <= 0x20 || decoded == 0x7f)
                return std::nullopt;
            canonical.push_back('%');
            canonical.push_back(upperAsciiHex(userInfo[index + 1]));
            canonical.push_back(upperAsciiHex(userInfo[index + 2]));
            index += 2;
            continue;
        }
        const bool subDelimiter = value == '!' || value == '$' || value == '&' || value == '\'' ||
                                  value == '(' || value == ')' || value == '*' || value == '+' ||
                                  value == ',' || value == ';' || value == '=';
        if (!isUnreserved(value) && !subDelimiter)
            return std::nullopt;
        canonical.push_back(value);
    }
    return canonical;
}

Result<std::string> canonicalizeHostAndPort(std::string_view hostAndPort) {
    if (hostAndPort.empty())
        return std::nullopt;

    std::string host;
    std::string port;
    if (hostAndPort.front() == '[') {
        const auto close = hostAndPort.find(']');
        if (close == std::string_view::npos || close == 1)
            return std::nullopt;
        const std::string_view literal = hostAndPort.substr(1, close - 1);
        if (literal.find(':') == std::string_view::npos)
            return std::nullopt;
        for (usize index = 0; index < literal.size(); ++index) {
            const char value = literal[index];
            if (value == '%') {
                if (index + 2 >= literal.size() || !isAsciiHex(literal[index + 1]) ||
                    !isAsciiHex(literal[index + 2])) {
                    return std::nullopt;
                }
                host.push_back('%');
                host.push_back(upperAsciiHex(literal[index + 1]));
                host.push_back(upperAsciiHex(literal[index + 2]));
                index += 2;
            } else if (isAsciiHex(value) || value == ':' || value == '.' || value == '-' ||
                       value == '_' || value == '~' || isAsciiAlpha(value)) {
                host.push_back(lowerAsciiChar(value));
            } else {
                return std::nullopt;
            }
        }
        host = "[" + host + "]";
        if (close + 1 < hostAndPort.size()) {
            if (hostAndPort[close + 1] != ':')
                return std::nullopt;
            port = std::string(hostAndPort.substr(close + 2));
        }
    } else {
        const auto colon = hostAndPort.rfind(':');
        if (colon != std::string_view::npos) {
            if (hostAndPort.find(':') != colon)
                return std::nullopt;
            host = std::string(hostAndPort.substr(0, colon));
            port = std::string(hostAndPort.substr(colon + 1));
        } else {
            host = std::string(hostAndPort);
        }
        if (host.empty())
            return std::nullopt;
        for (char& value : host) {
            if (!isAsciiAlpha(value) && !isAsciiDigit(value) && value != '.' && value != '-' &&
                value != '_') {
                return std::nullopt;
            }
            value = lowerAsciiChar(value);
        }
    }

    if (!port.empty()) {
        if (!std::all_of(port.begin(), port.end(), isAsciiDigit))
            return std::nullopt;
        unsigned long number = 0;
        for (char digit : port) {
            number = number * 10 + static_cast<unsigned long>(digit - '0');
            if (number > 65535)
                return std::nullopt;
        }
        if (number == 0)
            return std::nullopt;
        port = std::to_string(number);
    } else if (!hostAndPort.empty() && hostAndPort.back() == ':') {
        return std::nullopt;
    }

    return port.empty() ? host : host + ":" + port;
}

Result<std::string> canonicalizeAuthority(std::string_view authority) {
    if (authority.empty())
        return std::nullopt;

    const auto at = authority.find('@');
    if (at != std::string_view::npos && authority.find('@', at + 1) != std::string_view::npos)
        return std::nullopt;

    std::string userPrefix;
    std::string_view hostAndPort = authority;
    if (at != std::string_view::npos) {
        auto user = canonicalizeUserInfo(authority.substr(0, at));
        if (!user)
            return std::nullopt;
        userPrefix = *user + "@";
        hostAndPort = authority.substr(at + 1);
    }

    auto host = canonicalizeHostAndPort(hostAndPort);
    if (!host)
        return std::nullopt;
    return userPrefix + *host;
}

Result<std::string> canonicalizeSegment(std::string_view segment) {
    if (segment.empty())
        return std::nullopt;

    std::string canonical;
    std::string decoded;
    for (usize index = 0; index < segment.size(); ++index) {
        const char value = segment[index];
        if (value == '%') {
            if (index + 2 >= segment.size() || !isAsciiHex(segment[index + 1]) ||
                !isAsciiHex(segment[index + 2])) {
                return std::nullopt;
            }
            const auto byte = static_cast<unsigned char>((hexValue(segment[index + 1]) << 4) |
                                                         hexValue(segment[index + 2]));
            if (byte < 0x20 || byte == 0x7f || byte == '/' || byte == '\\')
                return std::nullopt;
            decoded.push_back(static_cast<char>(byte));
            if (byte < 0x80 && isUnreserved(static_cast<char>(byte))) {
                canonical.push_back(static_cast<char>(byte));
            } else {
                constexpr char hex[] = "0123456789ABCDEF";
                canonical.push_back('%');
                canonical.push_back(hex[(byte >> 4) & 0x0f]);
                canonical.push_back(hex[byte & 0x0f]);
            }
            index += 2;
        } else {
            if (!isUnreserved(value))
                return std::nullopt;
            decoded.push_back(value);
            canonical.push_back(value);
        }
    }

    if (decoded == "." || decoded == "..")
        return std::nullopt;
    return canonical;
}

Result<ParsedUrl> parseNetworkUrl(std::string_view raw) {
    if (raw.empty() || raw.find('?') != std::string_view::npos ||
        raw.find('#') != std::string_view::npos) {
        return std::nullopt;
    }
    for (char value : raw) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte <= 0x20 || byte >= 0x7f)
            return std::nullopt;
    }

    const auto separator = raw.find("://");
    if (separator == std::string_view::npos || separator == 0)
        return std::nullopt;
    std::string scheme = lowerAscii(std::string(raw.substr(0, separator)));
    if (!isAllowedScheme(scheme))
        return std::nullopt;

    const usize authorityStart = separator + 3;
    const usize pathStart = raw.find('/', authorityStart);
    const std::string_view rawAuthority =
        raw.substr(authorityStart,
                   (pathStart == std::string_view::npos ? raw.size() : pathStart) - authorityStart);
    auto authority = canonicalizeAuthority(rawAuthority);
    if (!authority)
        return std::nullopt;

    std::vector<std::string> segments;
    if (pathStart != std::string_view::npos) {
        std::string_view path = raw.substr(pathStart + 1);
        while (!path.empty()) {
            const auto slash = path.find('/');
            const auto rawSegment = path.substr(0, slash);
            auto segment = canonicalizeSegment(rawSegment);
            if (!segment)
                return std::nullopt;
            segments.push_back(*segment);
            if (slash == std::string_view::npos)
                break;
            path.remove_prefix(slash + 1);
            if (path.empty())
                break;
        }
    }

    std::string canonical = scheme + "://" + *authority;
    for (const auto& segment : segments)
        canonical += "/" + segment;
    return ParsedUrl{
        std::move(canonical), std::move(scheme), std::move(*authority), std::move(segments)};
}

std::string percentEncodePathSegment(std::string_view segment) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const char raw : segment) {
        const auto byte = static_cast<unsigned char>(raw);
        const char value = static_cast<char>(byte);
        if (byte < 0x80 && isUnreserved(value)) {
            encoded.push_back(value);
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(byte >> 4) & 0x0f]);
            encoded.push_back(hex[byte & 0x0f]);
        }
    }
    return encoded;
}

Result<ParsedKioFusePath> parseKioFusePath(const Path& path, const Path& runtimeRoot) {
    if (!isKioFuseShaped(path, runtimeRoot))
        return std::nullopt;

    const Path relative = path.lexically_relative(runtimeRoot);
    std::vector<std::string> components;
    for (const auto& component : relative)
        components.push_back(component.string());
    if (!components.empty() && components.back().empty())
        components.pop_back();
    if (components.size() < 3 || !isValidMountName(components[0]))
        return std::nullopt;

    const std::string scheme = lowerAscii(components[1]);
    if (!isAllowedScheme(scheme))
        return std::nullopt;
    auto authority = canonicalizeAuthority(components[2]);
    if (!authority)
        return std::nullopt;

    std::vector<std::string> remoteSegments;
    for (usize index = 3; index < components.size(); ++index) {
        if (components[index].empty() || components[index] == "." || components[index] == "..")
            return std::nullopt;
        remoteSegments.push_back(components[index]);
    }

    Path relativePath;
    for (usize index = 1; index < components.size(); ++index)
        relativePath /= components[index];
    return ParsedKioFusePath{runtimeRoot,
                             runtimeRoot / components[0],
                             std::move(relativePath),
                             scheme,
                             std::move(*authority),
                             std::move(remoteSegments)};
}

Result<std::string> makeDurableUrl(const ParsedKioFusePath& parsed) {
    std::string url = parsed.scheme + "://" + parsed.authority;
    for (const auto& segment : parsed.remoteSegments)
        url += "/" + percentEncodePathSegment(segment);
    auto canonical = parseNetworkUrl(url);
    return canonical ? Result<std::string>(canonical->canonical) : std::nullopt;
}

bool pathExists(const Path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

Result<Path> validatedMountedPath(const Path& candidate,
                                  const std::string& requestedUrl,
                                  const Path& runtimeRoot) {
    if (candidate.empty() || !candidate.is_absolute() || !pathExists(candidate))
        return std::nullopt;
    auto parsed = parseKioFusePath(candidate, runtimeRoot);
    if (!parsed)
        return std::nullopt;
    std::error_code linkError;
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(parsed->mountRoot, linkError)) ||
        linkError) {
        return std::nullopt;
    }
    auto url = makeDurableUrl(*parsed);
    if (!url || *url != requestedUrl)
        return std::nullopt;
    return candidate;
}

struct InFlightMount {
    std::condition_variable ready;
    bool complete = false;
    Result<Path> result;
};

std::mutex mountCacheMutex;
std::unordered_map<std::string, Path> mountedPathCache;
std::unordered_map<std::string, std::shared_ptr<InFlightMount>> inFlightMounts;

std::string mountCacheKey(const Path& runtimeRoot, const std::string& url) {
    return runtimeRoot.string() + "\n" + url;
}

Result<Path> cachedMountedPath(const std::string& key,
                               const std::string& url,
                               const Path& runtimeRoot) {
    Path candidate;
    {
        std::lock_guard<std::mutex> lock(mountCacheMutex);
        const auto found = mountedPathCache.find(key);
        if (found == mountedPathCache.end())
            return std::nullopt;
        candidate = found->second;
    }

    if (auto validated = validatedMountedPath(candidate, url, runtimeRoot))
        return validated;

    std::lock_guard<std::mutex> lock(mountCacheMutex);
    const auto found = mountedPathCache.find(key);
    if (found != mountedPathCache.end() && found->second == candidate)
        mountedPathCache.erase(found);
    return std::nullopt;
}

void cacheMountedPath(const std::string& key, const Path& path) {
    std::lock_guard<std::mutex> lock(mountCacheMutex);
    mountedPathCache[key] = path;
}

Result<Path> rebaseToCurrentMount(const ParsedKioFusePath& parsed, const std::string& url) {
    std::error_code error;
    std::filesystem::directory_iterator entries(parsed.runtimeRoot, error);
    if (error)
        return std::nullopt;

    for (const auto& entry : entries) {
        const std::string name = entry.path().filename().string();
        if (!isValidMountName(name) || entry.path() == parsed.mountRoot)
            continue;
        const Path candidate = entry.path() / parsed.relativePath;
        if (auto validated = validatedMountedPath(candidate, url, parsed.runtimeRoot))
            return validated;
    }
    return std::nullopt;
}

#ifdef __linux__

struct DbusError {
    DBusError value;
    DbusError() { dbus_error_init(&value); }
    ~DbusError() {
        if (dbus_error_is_set(&value))
            dbus_error_free(&value);
    }
};

struct DbusConnectionDeleter {
    void operator()(DBusConnection* connection) const {
        if (connection) {
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
        }
    }
};

struct DbusMessageDeleter {
    void operator()(DBusMessage* message) const {
        if (message)
            dbus_message_unref(message);
    }
};

Result<Path> mountWithKioFuse(const std::string& url) {
    static const bool threadsReady = dbus_threads_init_default() != 0;
    if (!threadsReady)
        return std::nullopt;

    DbusError error;
    std::unique_ptr<DBusConnection, DbusConnectionDeleter> connection(
        dbus_bus_get_private(DBUS_BUS_SESSION, &error.value));
    if (!connection || dbus_error_is_set(&error.value))
        return std::nullopt;
    dbus_connection_set_exit_on_disconnect(connection.get(), false);

    std::unique_ptr<DBusMessage, DbusMessageDeleter> request(dbus_message_new_method_call(
        "org.kde.KIOFuse", "/org/kde/KIOFuse", "org.kde.KIOFuse.VFS", "mountUrl"));
    if (!request)
        return std::nullopt;

    const char* rawUrl = url.c_str();
    if (!dbus_message_append_args(request.get(), DBUS_TYPE_STRING, &rawUrl, DBUS_TYPE_INVALID))
        return std::nullopt;

    constexpr int kMountTimeoutMs = 5000;
    std::unique_ptr<DBusMessage, DbusMessageDeleter> reply(
        dbus_connection_send_with_reply_and_block(
            connection.get(), request.get(), kMountTimeoutMs, &error.value));
    if (!reply || dbus_error_is_set(&error.value))
        return std::nullopt;

    const char* mountedPath = nullptr;
    if (!dbus_message_get_args(
            reply.get(), &error.value, DBUS_TYPE_STRING, &mountedPath, DBUS_TYPE_INVALID) ||
        dbus_error_is_set(&error.value) || !mountedPath || mountedPath[0] == '\0') {
        return std::nullopt;
    }
    return Path(mountedPath);
}

#else

Result<Path> mountWithKioFuse(const std::string&) {
    return std::nullopt;
}

#endif

} // namespace

Result<std::string> durableUrl(const Path& path, const Path& runtimeRootOverride) {
    if (auto existing = parseNetworkUrl(path.string()))
        return existing->canonical;
    const Path runtimeRoot = normalizedRuntimeRoot(runtimeRootOverride);
    if (auto parsed = parseKioFusePath(path, runtimeRoot))
        return makeDurableUrl(*parsed);
    return std::nullopt;
}

bool isNetworkLocation(const Path& path, const Path& runtimeRoot) {
    return durableUrl(path, runtimeRoot).has_value();
}

bool isNetworkLocationCandidate(const Path& path, const Path& runtimeRootOverride) {
    if (hasUrlMarker(path))
        return true;
    return isKioFuseShaped(path, normalizedRuntimeRoot(runtimeRootOverride));
}

Path parentLocation(const Path& path, const Path& runtimeRoot) {
    auto url = durableUrl(path, runtimeRoot);
    if (url) {
        auto parsed = parseNetworkUrl(*url);
        if (!parsed || parsed->segments.empty())
            return {};
        parsed->segments.pop_back();
        std::string parent = parsed->scheme + "://" + parsed->authority;
        for (const auto& segment : parsed->segments)
            parent += "/" + segment;
        return Path(parent);
    }
    if (isNetworkLocationCandidate(path, runtimeRoot))
        return {};
    return path.parent_path();
}

Path resolve(const Path& path, const Mounter& mounter, const Path& runtimeRootOverride) {
    if (path.empty())
        return path;

    const Path runtimeRoot = normalizedRuntimeRoot(runtimeRootOverride);
    const auto parsedPath = parseKioFusePath(path, runtimeRoot);
    const auto url = parsedPath ? makeDurableUrl(*parsedPath) : durableUrl(path, runtimeRoot);
    if (!url)
        return path;

    const std::string cacheKey = mountCacheKey(runtimeRoot, *url);

    if (parsedPath) {
        if (auto existing = validatedMountedPath(path, *url, runtimeRoot)) {
            cacheMountedPath(cacheKey, *existing);
            return *existing;
        }
    }

    if (auto cached = cachedMountedPath(cacheKey, *url, runtimeRoot))
        return *cached;

    if (parsedPath) {
        if (auto rebased = rebaseToCurrentMount(*parsedPath, *url)) {
            cacheMountedPath(cacheKey, *rebased);
            return *rebased;
        }
    }

    std::shared_ptr<InFlightMount> inFlight;
    bool mountLeader = false;
    {
        std::unique_lock<std::mutex> lock(mountCacheMutex);
        const auto cached = mountedPathCache.find(cacheKey);
        if (cached != mountedPathCache.end())
            return cached->second;

        const auto pending = inFlightMounts.find(cacheKey);
        if (pending != inFlightMounts.end()) {
            inFlight = pending->second;
            inFlight->ready.wait(lock, [&inFlight]() { return inFlight->complete; });
            return inFlight->result.value_or(path);
        }

        inFlight = std::make_shared<InFlightMount>();
        inFlightMounts.emplace(cacheKey, inFlight);
        mountLeader = true;
    }

    Result<Path> mounted;
    try {
        mounted = mounter ? mounter(*url) : mountWithKioFuse(*url);
    } catch (...) {
        mounted = std::nullopt;
    }

    Result<Path> validated;
    if (mounted)
        validated = validatedMountedPath(*mounted, *url, runtimeRoot);

    if (mountLeader) {
        {
            std::lock_guard<std::mutex> lock(mountCacheMutex);
            if (validated)
                mountedPathCache[cacheKey] = *validated;
            inFlight->result = validated;
            inFlight->complete = true;
            const auto pending = inFlightMounts.find(cacheKey);
            if (pending != inFlightMounts.end() && pending->second == inFlight)
                inFlightMounts.erase(pending);
        }
        inFlight->ready.notify_all();
    }
    return validated.value_or(path);
}

} // namespace network_location
} // namespace dw
