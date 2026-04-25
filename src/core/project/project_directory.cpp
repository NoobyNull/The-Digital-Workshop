#include "project_directory.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "../mesh/hash.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

namespace dw {

static constexpr const char* kLogModule = "ProjectDir";
static constexpr const char* kManifestFile = "project.json";
static constexpr int kManifestVersion = 1;

// --- sanitizeName ---

std::string ProjectDirectory::sanitizeName(const std::string& raw) {
    // Strip extension
    std::string stem = file::getStem(raw);
    if (stem.empty()) stem = raw;

    std::string out;
    out.reserve(stem.size());
    for (char c : stem) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    // Trim trailing dash
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "project";
    return out;
}

// --- createSubdirs ---

bool ProjectDirectory::createSubdirs() {
    bool ok = true;
    ok &= file::createDirectories(modelsDir());
    ok &= file::createDirectories(heightmapsDir());
    ok &= file::createDirectories(gcodeDir());
    ok &= file::createDirectories(imagesDir());
    ok &= file::createDirectories(costingDir());
    return ok;
}

// --- create ---

bool ProjectDirectory::create(const Path& root, const std::string& name,
                               const std::string& description) {
    m_root = root;
    m_name = name;
    m_description = description;
    m_models.clear();
    m_heightmaps.clear();
    m_gcodeFiles.clear();

    if (!file::createDirectories(m_root)) {
        log::errorf(kLogModule, "Failed to create directory: %s", m_root.c_str());
        return false;
    }
    if (!createSubdirs()) {
        log::errorf(kLogModule, "Failed to create subdirectories in: %s", m_root.c_str());
        return false;
    }
    if (!save()) {
        return false;
    }

    log::infof(kLogModule, "Created project: %s at %s", name.c_str(), m_root.c_str());
    return true;
}

// --- open ---

bool ProjectDirectory::open(const Path& root) {
    m_root = root;
    Path manifestPath = m_root / kManifestFile;

    auto text = file::readText(manifestPath);
    if (!text) {
        log::errorf(kLogModule, "Cannot read manifest: %s", manifestPath.c_str());
        return false;
    }

    try {
        auto j = nlohmann::json::parse(*text);

        m_name = j.value("name", "");
        m_description = j.value("description", "");

        m_models.clear();
        if (j.contains("models")) {
            for (const auto& m : j["models"]) {
                ProjectModelEntry e;
                e.filename = m.value("filename", "");
                e.hash = m.value("hash", "");
                m_models.push_back(std::move(e));
            }
        }

        m_heightmaps.clear();
        if (j.contains("heightmaps")) {
            for (const auto& h : j["heightmaps"]) {
                ProjectHeightmapEntry e;
                e.filename = h.value("filename", "");
                e.resolutionMmPerPx = h.value("resolutionMmPerPx", 0.0f);
                m_heightmaps.push_back(std::move(e));
            }
        }

        m_gcodeFiles.clear();
        if (j.contains("gcode")) {
            for (const auto& g : j["gcode"]) {
                ProjectGCodeEntry e;
                e.filename = g.value("filename", "");
                e.toolDescription = g.value("toolDescription", "");
                m_gcodeFiles.push_back(std::move(e));
            }
        }
    } catch (const nlohmann::json::exception& ex) {
        log::errorf(kLogModule, "JSON parse error: %s", ex.what());
        return false;
    }

    // Ensure subdirectories exist (may have been deleted or never created)
    createSubdirs();

    log::infof(kLogModule, "Opened project: %s at %s", m_name.c_str(), m_root.c_str());
    return true;
}

// --- save ---

bool ProjectDirectory::save() {
    nlohmann::json j;
    j["version"] = kManifestVersion;
    j["name"] = m_name;
    j["description"] = m_description;

    j["models"] = nlohmann::json::array();
    for (const auto& m : m_models) {
        j["models"].push_back({{"filename", m.filename}, {"hash", m.hash}});
    }

    j["heightmaps"] = nlohmann::json::array();
    for (const auto& h : m_heightmaps) {
        j["heightmaps"].push_back({
            {"filename", h.filename},
            {"resolutionMmPerPx", h.resolutionMmPerPx}
        });
    }

    j["gcode"] = nlohmann::json::array();
    for (const auto& g : m_gcodeFiles) {
        j["gcode"].push_back({
            {"filename", g.filename},
            {"toolDescription", g.toolDescription}
        });
    }

    Path manifestPath = m_root / kManifestFile;
    if (!file::writeText(manifestPath, j.dump(2))) {
        log::errorf(kLogModule, "Failed to write manifest: %s", manifestPath.c_str());
        return false;
    }
    return true;
}

void ProjectDirectory::setMetadata(const std::string& name, const std::string& description) {
    m_name = name;
    m_description = description;
}

void ProjectDirectory::clearModels() {
    m_models.clear();
}

void ProjectDirectory::clearGCode() {
    m_gcodeFiles.clear();
}

// --- addModelFile ---

bool ProjectDirectory::addModelFile(const Path& sourcePath) {
    return addModelFile(sourcePath, hash::computeFile(sourcePath));
}

bool ProjectDirectory::addModelFile(const Path& sourcePath, const std::string& fileHash) {
    if (!file::isFile(sourcePath)) {
        log::errorf(kLogModule, "Source model not found: %s", sourcePath.c_str());
        return false;
    }

    std::string ext = file::getExtension(sourcePath);
    std::string stem = file::getStem(sourcePath);
    std::string destName = stem + "-" + fileHash.substr(0, 8) + "." + ext;
    Path destPath = modelsDir() / destName;

    // Skip if already registered with same hash
    for (const auto& m : m_models) {
        if (m.hash == fileHash) return true;
    }

    if (!file::exists(destPath)) {
        if (!file::copy(sourcePath, destPath)) {
            log::errorf(kLogModule, "Failed to copy model to project: %s", destPath.c_str());
            return false;
        }
    }

    ProjectModelEntry entry;
    entry.filename = destName;
    entry.hash = fileHash;
    m_models.push_back(std::move(entry));
    return true;
}

// --- addGCodeFile ---

bool ProjectDirectory::addGCodeFile(const Path& sourcePath, const std::string& toolDescription) {
    if (!file::isFile(sourcePath)) {
        log::errorf(kLogModule, "Source G-code not found: %s", sourcePath.c_str());
        return false;
    }

    std::string fileHash = hash::computeFile(sourcePath);
    std::string ext = file::getExtension(sourcePath);
    std::string stem = file::getStem(sourcePath);
    std::string destName = stem + "-" + fileHash.substr(0, 8);
    if (!ext.empty()) {
        destName += "." + ext;
    }
    Path destPath = gcodeDir() / destName;

    for (const auto& g : m_gcodeFiles) {
        if (g.filename == destName) return true;
    }

    if (!file::exists(destPath)) {
        if (!file::copy(sourcePath, destPath)) {
            log::errorf(kLogModule, "Failed to copy G-code to project: %s", destPath.c_str());
            return false;
        }
    }

    m_gcodeFiles.push_back({destName, toolDescription});
    return true;
}

// --- addHeightmap ---

void ProjectDirectory::addHeightmap(const std::string& filename, f32 resolutionMmPerPx) {
    // Update existing entry or add new
    for (auto& h : m_heightmaps) {
        if (h.filename == filename) {
            h.resolutionMmPerPx = resolutionMmPerPx;
            return;
        }
    }
    m_heightmaps.push_back({filename, resolutionMmPerPx});
}

// --- addGCode ---

void ProjectDirectory::addGCode(const std::string& filename,
                                 const std::string& toolDescription) {
    for (auto& g : m_gcodeFiles) {
        if (g.filename == filename) {
            g.toolDescription = toolDescription;
            return;
        }
    }
    m_gcodeFiles.push_back({filename, toolDescription});
}

} // namespace dw
