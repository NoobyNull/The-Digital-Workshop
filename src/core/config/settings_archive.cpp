#include "settings_archive.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "config.h"
#include "../paths/app_paths.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

// version.h is generated in the build directory
#include "version.h"

namespace dw {

namespace {

// Binary archive format — same structure as ProjectArchive
constexpr uint32_t MAGIC = 0x44575300; // "DWS\0"
constexpr uint32_t FORMAT_VERSION = 1;

struct Header {
    uint32_t magic = MAGIC;
    uint32_t version = FORMAT_VERSION;
    uint32_t entryCount = 0;
    uint32_t reserved = 0;
};

bool writeU32(std::ostream& out, uint32_t value) {
    return out.write(reinterpret_cast<const char*>(&value), sizeof(value)).good();
}

bool writeU64(std::ostream& out, uint64_t value) {
    return out.write(reinterpret_cast<const char*>(&value), sizeof(value)).good();
}

bool readU32(std::istream& in, uint32_t& value) {
    return !in.read(reinterpret_cast<char*>(&value), sizeof(value)).fail();
}

bool readU64(std::istream& in, uint64_t& value) {
    return !in.read(reinterpret_cast<char*>(&value), sizeof(value)).fail();
}

void addFileEntry(std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries,
                  const std::string& archiveName, const Path& filePath) {
    auto data = file::readBinary(filePath.string());
    if (data && !data->empty()) {
        entries.emplace_back(archiveName, std::move(*data));
    }
}

void addStringEntry(std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries,
                    const std::string& archiveName, const std::string& content) {
    std::vector<uint8_t> data(content.begin(), content.end());
    entries.emplace_back(archiveName, std::move(data));
}

// Extract a specific section from config.ini text
std::string extractIniSection(const std::string& iniContent, const std::string& section) {
    std::string header = "[" + section + "]";
    auto pos = iniContent.find(header);
    if (pos == std::string::npos)
        return "";

    auto end = iniContent.find("\n[", pos + header.size());
    if (end == std::string::npos)
        end = iniContent.size();

    return iniContent.substr(pos, end - pos);
}

// Build the manifest JSON
std::string buildManifest(const std::vector<SettingsCategory>& cats) {
    nlohmann::json j;
    j["version"] = 1;
    j["appVersion"] = VERSION;

    // ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", std::localtime(&time));
    j["exportDate"] = timeBuf;

    // Hostname
    char hostBuf[256] = {};
    gethostname(hostBuf, sizeof(hostBuf));
    j["hostname"] = hostBuf;

    nlohmann::json catArr = nlohmann::json::array();
    for (auto c : cats)
        catArr.push_back(static_cast<int>(c));
    j["categories"] = catArr;

    return j.dump(2);
}

// Map category to config.ini section names
std::vector<std::string> iniSectionsForCategory(SettingsCategory cat) {
    switch (cat) {
    case SettingsCategory::UIPreferences:  return {"ui", "render", "logging"};
    case SettingsCategory::MachineProfiles: return {"machine_profiles"};
    case SettingsCategory::LayoutPresets:  return {"layout_presets"};
    case SettingsCategory::InputBindings:  return {"bindings"};
    case SettingsCategory::CncSettings:    return {"cnc", "safety", "wcs_aliases"};
    case SettingsCategory::DirectoryPaths: return {"paths", "dirs"};
    case SettingsCategory::ImportSettings: return {"import", "materials"};
    case SettingsCategory::RecentFiles:    return {"recent", "recent_gcode"};
    case SettingsCategory::ApiKeys:        return {"api"};
    default: return {};
    }
}

} // namespace

const char* settingsCategoryName(SettingsCategory cat) {
    switch (cat) {
    case SettingsCategory::UIPreferences:   return "UI Preferences";
    case SettingsCategory::WindowLayout:    return "Window Layout";
    case SettingsCategory::MachineProfiles: return "Machine Profiles";
    case SettingsCategory::LayoutPresets:   return "Layout Presets";
    case SettingsCategory::InputBindings:   return "Input Bindings";
    case SettingsCategory::CncSettings:     return "CNC Settings";
    case SettingsCategory::DirectoryPaths:  return "Directory Paths";
    case SettingsCategory::ImportSettings:  return "Import Settings";
    case SettingsCategory::ToolDatabase:    return "Tool Database";
    case SettingsCategory::RecentFiles:     return "Recent Files";
    case SettingsCategory::ApiKeys:         return "API Keys";
    default:                                return "Unknown";
    }
}

const char* settingsCategoryDescription(SettingsCategory cat) {
    switch (cat) {
    case SettingsCategory::UIPreferences:
        return "Theme, scale, navigation style, 3D rendering (lighting, colors)";
    case SettingsCategory::WindowLayout:
        return "Window positions, sizes, and docking layout";
    case SettingsCategory::MachineProfiles:
        return "CNC machine definitions (feeds, travel, spindle, capabilities)";
    case SettingsCategory::LayoutPresets:
        return "Panel visibility presets (Modeling, CNC, custom)";
    case SettingsCategory::InputBindings:
        return "Keyboard and mouse shortcuts";
    case SettingsCategory::CncSettings:
        return "CNC operation, safety, jog speeds, WCS aliases";
    case SettingsCategory::DirectoryPaths:
        return "Workspace root, model/project/gcode/material directories";
    case SettingsCategory::ImportSettings:
        return "Import parallelism, file handling, default material";
    case SettingsCategory::ToolDatabase:
        return "CNC tool library (.vtdb — Vectric-compatible)";
    case SettingsCategory::RecentFiles:
        return "Recently opened projects and G-code files";
    case SettingsCategory::ApiKeys:
        return "LM Studio endpoint and other service credentials";
    default:
        return "";
    }
}

bool exportSettings(const Path& archivePath) {
    Path configDir = paths::getConfigDir();
    Path configIni = configDir / "config.ini";
    Path imguiIni = configDir / "imgui.ini";
    Path toolsVtdb = paths::getToolDatabasePath();

    // Read config.ini text for manifest building
    auto configText = file::readText(configIni.string());
    if (!configText) {
        log::error("SettingsArchive", "Cannot read config.ini for export");
        return false;
    }

    // Determine which categories have data
    std::vector<SettingsCategory> cats;
    for (int i = 0; i < static_cast<int>(SettingsCategory::Count); ++i) {
        auto cat = static_cast<SettingsCategory>(i);
        if (cat == SettingsCategory::WindowLayout) {
            if (file::exists(imguiIni))
                cats.push_back(cat);
        } else if (cat == SettingsCategory::ToolDatabase) {
            if (file::exists(toolsVtdb))
                cats.push_back(cat);
        } else {
            // Check if any of the INI sections for this category have content
            auto sections = iniSectionsForCategory(cat);
            for (const auto& sec : sections) {
                if (!extractIniSection(*configText, sec).empty()) {
                    cats.push_back(cat);
                    break;
                }
            }
        }
    }

    // Build file entries
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;

    // Manifest
    addStringEntry(entries, "manifest.json", buildManifest(cats));

    // config.ini — always include full file
    addFileEntry(entries, "config.ini", configIni);

    // imgui.ini
    if (file::exists(imguiIni))
        addFileEntry(entries, "imgui.ini", imguiIni);

    // tools.vtdb
    if (file::exists(toolsVtdb))
        addFileEntry(entries, "tools.vtdb", toolsVtdb);

    // Write archive
    std::ofstream out(archivePath, std::ios::binary);
    if (!out) {
        log::errorf("SettingsArchive", "Cannot create archive: %s",
                    archivePath.c_str());
        return false;
    }

    Header header;
    header.entryCount = static_cast<uint32_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (const auto& [name, data] : entries) {
        uint32_t pathLen = static_cast<uint32_t>(name.size());
        writeU32(out, pathLen);
        out.write(name.data(), pathLen);
        writeU64(out, data.size());
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }

    out.close();
    if (!out) {
        log::error("SettingsArchive", "Failed to finalize archive");
        return false;
    }

    log::infof("SettingsArchive", "Exported %zu entries to: %s",
               entries.size(), archivePath.c_str());
    return true;
}

// Read a single file from the archive into memory
static bool readArchiveFile(const Path& archivePath, const std::string& targetName,
                            std::vector<uint8_t>& outData) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in)
        return false;

    Header header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.magic != MAGIC || header.version > FORMAT_VERSION)
        return false;

    for (uint32_t i = 0; i < header.entryCount; ++i) {
        uint32_t pathLen = 0;
        if (!readU32(in, pathLen) || pathLen > 4096)
            return false;

        std::string name(pathLen, '\0');
        in.read(name.data(), pathLen);

        uint64_t dataSize = 0;
        if (!readU64(in, dataSize))
            return false;

        if (name == targetName) {
            outData.resize(dataSize);
            in.read(reinterpret_cast<char*>(outData.data()),
                    static_cast<std::streamsize>(dataSize));
            return in.good();
        }

        in.seekg(static_cast<std::streamoff>(dataSize), std::ios::cur);
    }
    return false;
}

bool readSettingsManifest(const Path& archivePath, SettingsArchiveManifest& manifest) {
    std::vector<uint8_t> data;
    if (!readArchiveFile(archivePath, "manifest.json", data))
        return false;

    try {
        auto j = nlohmann::json::parse(data.begin(), data.end());
        manifest.version = j.value("version", 1);
        manifest.appVersion = j.value("appVersion", "");
        manifest.exportDate = j.value("exportDate", "");
        manifest.hostname = j.value("hostname", "");
        manifest.categories.clear();
        if (j.contains("categories")) {
            for (const auto& c : j["categories"])
                manifest.categories.push_back(static_cast<SettingsCategory>(c.get<int>()));
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool importSettings(const Path& archivePath,
                    const std::vector<SettingsCategory>& selected) {
    if (selected.empty())
        return false;

    // Read the full config.ini from archive
    std::vector<uint8_t> archivedConfig;
    bool hasConfig = readArchiveFile(archivePath, "config.ini", archivedConfig);
    std::string archivedIniText;
    if (hasConfig)
        archivedIniText.assign(archivedConfig.begin(), archivedConfig.end());

    // Read current config.ini
    Path configDir = paths::getConfigDir();
    Path configIni = configDir / "config.ini";
    auto currentText = file::readText(configIni.string());
    std::string currentIni = currentText ? *currentText : "";

    bool anyRestored = false;

    for (SettingsCategory cat : selected) {
        if (cat == SettingsCategory::WindowLayout) {
            // Replace imgui.ini
            std::vector<uint8_t> imguiData;
            if (readArchiveFile(archivePath, "imgui.ini", imguiData)) {
                Path imguiIni = configDir / "imgui.ini";
                if (file::writeBinary(imguiIni.string(), imguiData)) {
                    log::info("SettingsArchive", "Restored window layout (imgui.ini)");
                    anyRestored = true;
                } else {
                    log::warning("SettingsArchive", "Failed to restore window layout");
                }
            }
            continue;
        }

        if (cat == SettingsCategory::ToolDatabase) {
            // Replace tools.vtdb
            std::vector<uint8_t> vtdbData;
            if (readArchiveFile(archivePath, "tools.vtdb", vtdbData)) {
                Path vtdbPath = paths::getToolDatabasePath();
                if (file::writeBinary(vtdbPath.string(), vtdbData)) {
                    log::info("SettingsArchive", "Restored tool database (tools.vtdb)");
                    anyRestored = true;
                } else {
                    log::warning("SettingsArchive", "Failed to restore tool database");
                }
            }
            continue;
        }

        // INI-section-based categories: replace matching sections in current config
        if (!hasConfig)
            continue;

        auto sections = iniSectionsForCategory(cat);
        for (const auto& sec : sections) {
            std::string archivedSection = extractIniSection(archivedIniText, sec);
            if (archivedSection.empty())
                continue;

            std::string currentSection = extractIniSection(currentIni, sec);
            if (!currentSection.empty()) {
                // Replace existing section
                auto pos = currentIni.find(currentSection);
                if (pos != std::string::npos)
                    currentIni.replace(pos, currentSection.size(), archivedSection);
            } else {
                // Append new section
                if (!currentIni.empty() && currentIni.back() != '\n')
                    currentIni += "\n";
                currentIni += archivedSection + "\n";
            }
            anyRestored = true;
        }
    }

    // Write merged config.ini back
    if (anyRestored && hasConfig) {
        std::ofstream out(configIni.string());
        out << currentIni;
        out.close();

        // Reload config from disk
        Config::instance().load();
        log::info("SettingsArchive", "Config reloaded after import");
    }

    return anyRestored;
}

} // namespace dw
