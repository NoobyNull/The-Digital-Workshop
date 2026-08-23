// User-selected paths and recent-project history. Restart-resume state lives
// in its own atomic sidecar rather than the general settings document.

#include "config.h"

#include <algorithm>

#include "../paths/network_location.h"
#include "../utils/file_utils.h"
#include "../utils/string_utils.h"

namespace dw {
namespace {

Path durableRecentLocation(const Path& path) {
    if (!network_location::isNetworkLocationCandidate(path))
        return path;
    if (auto url = network_location::durableUrl(path))
        return Path(*url);
    return {};
}

} // namespace

void Config::loadPaths(const std::string& key, const std::string& value) {
    if (key == "workspace_root") {
        m_workspaceRoot = value;
    } else if (key == "last_import") {
        m_lastImportDir = value;
    } else if (key == "last_export") {
        m_lastExportDir = value;
    }
}

void Config::savePaths(std::ostringstream& ss) const {
    ss << "[paths]\n";
    if (!m_workspaceRoot.empty())
        ss << "workspace_root=" << m_workspaceRoot.string() << "\n";
    if (!m_lastImportDir.empty())
        ss << "last_import=" << m_lastImportDir.string() << "\n";
    if (!m_lastExportDir.empty())
        ss << "last_export=" << m_lastExportDir.string() << "\n";
    ss << "\n";
}

void Config::loadRecent(const std::string& key, const std::string& value) {
    if (!str::startsWith(key, "project") || value.empty())
        return;

    const Path loadedPath(value);
    const Path path = durableRecentLocation(loadedPath);
    if (path.empty())
        return;
    const bool networkLocation = network_location::isNetworkLocation(path);
    // Network history must survive an offline startup. It is materialized only
    // when the user actually reopens it; ordinary missing local paths retain
    // the existing pruning behavior.
    if (!networkLocation && !file::exists(path))
        return;

    if (std::find(m_recentProjects.begin(), m_recentProjects.end(), path) ==
        m_recentProjects.end()) {
        m_recentProjects.push_back(path);
    }
}

void Config::saveRecent(std::ostringstream& ss) const {
    ss << "[recent]\n";
    for (std::size_t i = 0; i < m_recentProjects.size(); ++i)
        ss << "project" << i << "=" << m_recentProjects[i].string() << "\n";
    ss << "\n";
}

void Config::addRecentProject(const Path& path) {
    const Path durablePath = durableRecentLocation(path);
    if (durablePath.empty())
        return;
    removeRecentProject(durablePath);
    m_recentProjects.insert(m_recentProjects.begin(), durablePath);
    if (m_recentProjects.size() > MAX_RECENT_PROJECTS)
        m_recentProjects.resize(MAX_RECENT_PROJECTS);
}

void Config::removeRecentProject(const Path& path) {
    const Path durablePath = durableRecentLocation(path);
    if (durablePath.empty())
        return;
    const auto it = std::remove_if(
        m_recentProjects.begin(),
        m_recentProjects.end(),
        [&durablePath](const Path& recent) {
            return durableRecentLocation(recent) == durablePath;
        });
    m_recentProjects.erase(it, m_recentProjects.end());
}

void Config::clearRecentProjects() {
    m_recentProjects.clear();
}

} // namespace dw
