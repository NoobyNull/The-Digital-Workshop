#include "config.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

#include "../paths/app_paths.h"
#include "../paths/network_location.h"
#include "../paths/path_resolver.h"
#include "../threading/thread_pool.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace dw {
namespace {

Path durableRecentGCodeLocation(const Path& path) {
    if (!network_location::isNetworkLocationCandidate(path))
        return path;
    if (auto url = network_location::durableUrl(path))
        return Path(*url);
    return {};
}

} // namespace

Config::Config() : m_parallelismTier(ParallelismTier::Auto) {
    initDefaultBindings();
    m_machineProfiles = gcode::MachineProfile::allBuiltInPresets();
    m_layoutPresets = {
        LayoutPreset::guidedDefault(),
        LayoutPreset::advancedDefault(),
        LayoutPreset::cncDefault(),
    };
}

Config& Config::instance() {
    static Config instance;
    return instance;
}

Path Config::getConfigFilePath() const {
    return paths::getConfigDir() / "config.ini";
}

// ---------------------------------------------------------------------------
// load() — dispatcher
// ---------------------------------------------------------------------------

bool Config::load() {
    Path configPath = getConfigFilePath();

    if (!file::exists(configPath)) {
        log::info("Config", "No config file found, using defaults");
        return true;
    }

    auto content = file::readText(configPath);
    if (!content) {
        log::error("Config", "Failed to read config file");
        return false;
    }

    // A file without an explicit layout version is a legacy snapshot. The
    // resolver upgrades it after every preset has been read.
    m_layoutMigrationVersion = 0;

    std::istringstream stream(*content);
    std::string line;
    std::string section;
    std::optional<std::vector<gcode::MachineProfile>> loadedMachineProfiles;
    std::optional<std::vector<LayoutPreset>> loadedLayoutPresets;

    while (std::getline(stream, line)) {
        line = str::trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }

        // Key=Value pair
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = str::trim(line.substr(0, pos));
        std::string value = str::trim(line.substr(pos + 1));

        // Dispatch to per-section loader
        if (section == "ui") {
            loadUi(key, value);
        } else if (section == "render") {
            loadRender(key, value);
        } else if (section == "logging") {
            loadLogging(key, value);
        } else if (section == "paths") {
            loadPaths(key, value);
        } else if (section == "window") {
            loadWindow(key, value);
        } else if (section == "workspace") {
            loadWorkspace(key, value);
        } else if (section == "bindings") {
            loadBindings(key, value);
        } else if (section == "dirs") {
            loadDirs(key, value);
        } else if (section == "import") {
            loadImport(key, value);
        } else if (section == "materials") {
            loadMaterials(key, value);
        } else if (section == "api") {
            loadApi(key, value);
        } else if (section == "recent") {
            loadRecent(key, value);
        } else if (section == "safety") {
            loadSafety(key, value);
        } else if (section == "cnc") {
            loadCnc(key, value);
        } else if (section == "wcs_aliases") {
            loadWcsAliases(key, value);
        } else if (section == "recent_gcode") {
            loadRecentGCode(key, value);
        } else if (section == "machine_profiles") {
            loadMachineProfiles(key, value, loadedMachineProfiles);
        } else if (section == "layout_presets") {
            loadLayoutPresets(key, value, loadedLayoutPresets);
        }
    }

    resolveLoadedMachineProfiles(loadedMachineProfiles);
    resolveLoadedLayoutPresets(loadedLayoutPresets);

    log::infof("Config", "Loaded from %s", configPath.string().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Per-section load helpers
// ---------------------------------------------------------------------------

void Config::loadRender(const std::string& key, const std::string& value) {
    if (key == "light_dir_x")
        str::parseFloat(value, m_lightDir.x);
    else if (key == "light_dir_y")
        str::parseFloat(value, m_lightDir.y);
    else if (key == "light_dir_z")
        str::parseFloat(value, m_lightDir.z);
    else if (key == "light_color_r")
        str::parseFloat(value, m_lightColor.x);
    else if (key == "light_color_g")
        str::parseFloat(value, m_lightColor.y);
    else if (key == "light_color_b")
        str::parseFloat(value, m_lightColor.z);
    else if (key == "ambient_r")
        str::parseFloat(value, m_ambient.x);
    else if (key == "ambient_g")
        str::parseFloat(value, m_ambient.y);
    else if (key == "ambient_b")
        str::parseFloat(value, m_ambient.z);
    else if (key == "object_color_r")
        str::parseFloat(value, m_objectColor.r);
    else if (key == "object_color_g")
        str::parseFloat(value, m_objectColor.g);
    else if (key == "object_color_b")
        str::parseFloat(value, m_objectColor.b);
    else if (key == "shininess")
        str::parseFloat(value, m_shininess);
}

void Config::loadLogging(const std::string& key, const std::string& value) {
    if (key == "level") {
        str::parseInt(value, m_logLevel);
    } else if (key == "log_to_file") {
        m_logToFile = (value == "true" || value == "1");
    } else if (key == "log_file_path") {
        m_logFilePath = value;
    }
}

void Config::loadBindings(const std::string& key, const std::string& value) {
    if (key == "light_dir_drag") {
        m_bindings[static_cast<int>(BindAction::LightDirDrag)] =
            InputBinding::deserialize(value);
    } else if (key == "light_intensity_drag") {
        m_bindings[static_cast<int>(BindAction::LightIntensityDrag)] =
            InputBinding::deserialize(value);
    }
}

void Config::loadDirs(const std::string& key, const std::string& value) {
    if (key == "models") {
        m_modelsDir = value;
    } else if (key == "projects") {
        m_projectsDir = value;
    } else if (key == "materials") {
        m_materialsDir = value;
    } else if (key == "gcode") {
        m_gcodeDir = value;
    } else if (key == "support") {
        m_supportDir = value;
    }
}

void Config::loadImport(const std::string& key, const std::string& value) {
    if (key == "parallelism_tier") {
        int tier = 0;
        str::parseInt(value, tier);
        if (tier >= 0 && tier <= 2)
            m_parallelismTier = static_cast<ParallelismTier>(tier);
    } else if (key == "file_handling_mode") {
        int mode = 0;
        str::parseInt(value, mode);
        if (mode >= 0 && mode <= 2)
            m_fileHandlingMode = static_cast<FileHandlingMode>(mode);
    } else if (key == "library_dir") {
        m_libraryDir = value;
    } else if (key == "show_error_toasts") {
        m_showImportErrorToasts = (value == "true" || value == "1");
    }
}

void Config::loadMaterials(const std::string& key, const std::string& value) {
    if (key == "default_material_id") {
        str::parseInt64(value, m_defaultMaterialId);
    }
}

void Config::loadApi(const std::string& key, const std::string& value) {
    if (key == "lmstudio_endpoint") {
        m_lmStudioEndpoint = value;
    } else if (key == "ai_provider") {
        m_aiProvider = value;
    } else if (key == "ai_model") {
        m_aiModel = value;
    } else if (key == "ollama_endpoint") {
        m_ollamaEndpoint = value;
    } else if (key == "ollama_private_port") {
        str::parseInt(value, m_ollamaPrivatePort);
    }
}

void Config::loadSafety(const std::string& key, const std::string& value) {
    if (key == "long_press_enabled") {
        m_safetyLongPressEnabled = (value == "true" || value == "1");
    } else if (key == "long_press_duration_ms") {
        str::parseInt(value, m_safetyLongPressDurationMs);
    } else if (key == "abort_long_press") {
        m_safetyAbortLongPress = (value == "true" || value == "1");
    } else if (key == "dead_man_enabled") {
        m_safetyDeadManEnabled = (value == "true" || value == "1");
    } else if (key == "dead_man_timeout_ms") {
        str::parseInt(value, m_safetyDeadManTimeoutMs);
    } else if (key == "door_interlock_enabled") {
        m_safetyDoorInterlockEnabled = (value == "true" || value == "1");
    } else if (key == "soft_limit_check_enabled") {
        m_safetySoftLimitCheckEnabled = (value == "true" || value == "1");
    } else if (key == "pause_before_reset_enabled") {
        m_safetyPauseBeforeResetEnabled = (value == "true" || value == "1");
    }
}

void Config::loadCnc(const std::string& key, const std::string& value) {
    if (key == "status_poll_interval_ms") {
        int val = 200;
        str::parseInt(value, val);
        m_statusPollIntervalMs = std::clamp(val, 50, 200);
    } else if (key == "jog_feed_small") {
        int val = 200;
        str::parseInt(value, val);
        m_jogFeedSmall = std::clamp(val, 10, 2000);
    } else if (key == "jog_feed_medium") {
        int val = 1000;
        str::parseInt(value, val);
        m_jogFeedMedium = std::clamp(val, 100, 5000);
    } else if (key == "jog_feed_large") {
        int val = 3000;
        str::parseInt(value, val);
        m_jogFeedLarge = std::clamp(val, 500, 10000);
    } else if (key == "job_completion_notify") {
        m_jobCompletionNotify = (value == "true" || value == "1");
    } else if (key == "job_completion_flash") {
        m_jobCompletionFlash = (value == "true" || value == "1");
    } else if (key == "display_units") {
        m_displayUnitsMetric = (value != "in");
    } else if (key == "advanced_settings_view") {
        m_advancedSettingsView = (value == "true" || value == "1");
    } else if (key == "show_tool_dot") {
        m_cncShowToolDot = (value == "true" || value == "1");
    } else if (key == "show_work_envelope") {
        m_cncShowWorkEnvelope = (value == "true" || value == "1");
    } else if (key == "show_dro_overlay") {
        m_cncShowDroOverlay = (value == "true" || value == "1");
    } else if (key == "tool_dot_size") {
        str::parseFloat(value, m_cncToolDotSize);
    } else if (key == "tool_dot_color_r") {
        str::parseFloat(value, m_cncToolDotColor.x);
    } else if (key == "tool_dot_color_g") {
        str::parseFloat(value, m_cncToolDotColor.y);
    } else if (key == "tool_dot_color_b") {
        str::parseFloat(value, m_cncToolDotColor.z);
    } else if (key == "tool_dot_color_a") {
        str::parseFloat(value, m_cncToolDotColor.w);
    } else if (key == "envelope_color_r") {
        str::parseFloat(value, m_cncEnvelopeColor.x);
    } else if (key == "envelope_color_g") {
        str::parseFloat(value, m_cncEnvelopeColor.y);
    } else if (key == "envelope_color_b") {
        str::parseFloat(value, m_cncEnvelopeColor.z);
    } else if (key == "envelope_color_a") {
        str::parseFloat(value, m_cncEnvelopeColor.w);
    } else if (key == "show_model_bounds") {
        m_cncShowModelBounds = (value == "true" || value == "1");
    } else if (key == "model_bounds_in_color_r") {
        str::parseFloat(value, m_cncModelBoundsInColor.x);
    } else if (key == "model_bounds_in_color_g") {
        str::parseFloat(value, m_cncModelBoundsInColor.y);
    } else if (key == "model_bounds_in_color_b") {
        str::parseFloat(value, m_cncModelBoundsInColor.z);
    } else if (key == "model_bounds_in_color_a") {
        str::parseFloat(value, m_cncModelBoundsInColor.w);
    } else if (key == "model_bounds_out_color_r") {
        str::parseFloat(value, m_cncModelBoundsOutColor.x);
    } else if (key == "model_bounds_out_color_g") {
        str::parseFloat(value, m_cncModelBoundsOutColor.y);
    } else if (key == "model_bounds_out_color_b") {
        str::parseFloat(value, m_cncModelBoundsOutColor.z);
    } else if (key == "model_bounds_out_color_a") {
        str::parseFloat(value, m_cncModelBoundsOutColor.w);
    }
}

void Config::loadWcsAliases(const std::string& key, const std::string& value) {
    for (int wi = 0; wi < NUM_WCS; ++wi) {
        char wkey[8];
        std::snprintf(wkey, sizeof(wkey), "g%d", 54 + wi);
        if (key == wkey) {
            m_wcsAliases[wi] = value;
            break;
        }
    }
}

void Config::loadRecentGCode(const std::string& key, const std::string& value) {
    if (str::startsWith(key, "file")) {
        if (!value.empty() && static_cast<int>(m_recentGCodeFiles.size()) < MAX_RECENT_GCODE) {
            const Path path = durableRecentGCodeLocation(Path(value));
            if (!path.empty() &&
                std::find(m_recentGCodeFiles.begin(), m_recentGCodeFiles.end(), path) ==
                m_recentGCodeFiles.end()) {
                m_recentGCodeFiles.push_back(path);
            }
        }
    }
}

void Config::loadMachineProfiles(
    const std::string& key, const std::string& value,
    std::optional<std::vector<gcode::MachineProfile>>& loaded) {
    if (key == "active_profile") {
        // Try as name first, fall back to index for backwards compat
        int idx = -1;
        if (str::parseInt(value, idx) && idx >= 0)
            m_activeMachineProfileIndex = idx;
        else
            m_activeProfileName = value;
    } else if (str::startsWith(key, "profile")) {
        auto profile = gcode::MachineProfile::fromJsonString(value);
        if (!profile.name.empty()) {
            if (!loaded)
                loaded = std::vector<gcode::MachineProfile>{};
            loaded->push_back(std::move(profile));
        }
    }
}

// ---------------------------------------------------------------------------
// Post-load resolution
// ---------------------------------------------------------------------------

void Config::resolveLoadedMachineProfiles(
    std::optional<std::vector<gcode::MachineProfile>>& loaded) {
    // Built-in presets are always present; append any user-created profiles from config
    // m_machineProfiles already contains allBuiltInPresets() from constructor
    if (loaded && !loaded->empty()) {
        for (auto& lp : *loaded) {
            // Skip any that duplicate a built-in by name
            bool isDuplicate = false;
            for (const auto& bp : m_machineProfiles) {
                if (bp.name == lp.name) { isDuplicate = true; break; }
            }
            if (!isDuplicate) {
                lp.builtIn = false;
                m_machineProfiles.push_back(std::move(lp));
            }
        }
    }
    // Resolve active profile by name if stored, otherwise by index
    if (!m_activeProfileName.empty()) {
        for (int i = 0; i < static_cast<int>(m_machineProfiles.size()); ++i) {
            if (m_machineProfiles[static_cast<size_t>(i)].name == m_activeProfileName) {
                m_activeMachineProfileIndex = i;
                break;
            }
        }
        m_activeProfileName.clear();
    }
    if (m_activeMachineProfileIndex < 0 ||
        m_activeMachineProfileIndex >= static_cast<int>(m_machineProfiles.size())) {
        m_activeMachineProfileIndex = 0;
    }
}

// ---------------------------------------------------------------------------
// save() — dispatcher
// ---------------------------------------------------------------------------

bool Config::save() {
    Path configPath = getConfigFilePath();

    // Ensure config directory exists
    if (!file::createDirectories(configPath.parent_path())) {
        log::error("Config", "Failed to create config directory");
        return false;
    }

    std::ostringstream ss;
    ss << std::fixed;

    ss << "# Digital Workshop Configuration\n\n";

    saveUi(ss);
    saveRender(ss);
    saveLogging(ss);
    savePaths(ss);
    saveWindow(ss);
    saveWorkspace(ss);
    saveBindings(ss);
    saveDirs(ss);
    saveImport(ss);
    saveMaterials(ss);
    saveApi(ss);
    saveRecent(ss);
    saveCnc(ss);
    saveWcsAliases(ss);
    saveRecentGCode(ss);
    saveSafety(ss);
    saveMachineProfiles(ss);
    saveLayoutPresets(ss);

    // Atomic save: write to temp file, then rename
    Path tempPath = configPath.string() + ".tmp";
    if (!file::writeText(tempPath, ss.str())) {
        log::error("Config", "Failed to save config file");
        return false;
    }

    std::error_code ec;
    fs::rename(tempPath, configPath, ec);
    if (ec) {
        log::errorf("Config", "Failed to rename temp config: %s", ec.message().c_str());
        return false;
    }

    log::debugf("Config", "Saved to %s", configPath.string().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Per-section save helpers
// ---------------------------------------------------------------------------

void Config::saveRender(std::ostringstream& ss) const {
    ss << "[render]\n";
    ss << "light_dir_x=" << m_lightDir.x << "\n";
    ss << "light_dir_y=" << m_lightDir.y << "\n";
    ss << "light_dir_z=" << m_lightDir.z << "\n";
    ss << "light_color_r=" << m_lightColor.x << "\n";
    ss << "light_color_g=" << m_lightColor.y << "\n";
    ss << "light_color_b=" << m_lightColor.z << "\n";
    ss << "ambient_r=" << m_ambient.x << "\n";
    ss << "ambient_g=" << m_ambient.y << "\n";
    ss << "ambient_b=" << m_ambient.z << "\n";
    ss << "object_color_r=" << m_objectColor.r << "\n";
    ss << "object_color_g=" << m_objectColor.g << "\n";
    ss << "object_color_b=" << m_objectColor.b << "\n";
    ss << "shininess=" << m_shininess << "\n";
    ss << "\n";
}

void Config::saveLogging(std::ostringstream& ss) const {
    ss << "[logging]\n";
    ss << "level=" << m_logLevel << "\n";
    ss << "log_to_file=" << (m_logToFile ? "true" : "false") << "\n";
    if (!m_logFilePath.empty()) {
        ss << "log_file_path=" << m_logFilePath.string() << "\n";
    }
    ss << "\n";
}

void Config::saveBindings(std::ostringstream& ss) const {
    ss << "[bindings]\n";
    ss << "light_dir_drag=" << m_bindings[static_cast<int>(BindAction::LightDirDrag)].serialize()
       << "\n";
    ss << "light_intensity_drag="
       << m_bindings[static_cast<int>(BindAction::LightIntensityDrag)].serialize() << "\n";
    ss << "\n";
}

void Config::saveDirs(std::ostringstream& ss) const {
    ss << "[dirs]\n";
    if (!m_modelsDir.empty()) {
        ss << "models=" << m_modelsDir.string() << "\n";
    }
    if (!m_projectsDir.empty()) {
        ss << "projects=" << m_projectsDir.string() << "\n";
    }
    if (!m_materialsDir.empty()) {
        ss << "materials=" << m_materialsDir.string() << "\n";
    }
    if (!m_gcodeDir.empty()) {
        ss << "gcode=" << m_gcodeDir.string() << "\n";
    }
    if (!m_supportDir.empty()) {
        ss << "support=" << m_supportDir.string() << "\n";
    }
    ss << "\n";
}

void Config::saveImport(std::ostringstream& ss) const {
    ss << "[import]\n";
    ss << "parallelism_tier=" << static_cast<int>(m_parallelismTier) << "\n";
    ss << "file_handling_mode=" << static_cast<int>(m_fileHandlingMode) << "\n";
    if (!m_libraryDir.empty()) {
        ss << "library_dir=" << m_libraryDir.string() << "\n";
    }
    ss << "show_error_toasts=" << (m_showImportErrorToasts ? "true" : "false") << "\n";
    ss << "\n";
}

void Config::saveMaterials(std::ostringstream& ss) const {
    ss << "[materials]\n";
    ss << "default_material_id=" << m_defaultMaterialId << "\n";
    ss << "\n";
}

void Config::saveApi(std::ostringstream& ss) const {
    ss << "[api]\n";
    ss << "ai_provider=" << m_aiProvider << "\n";
    ss << "ai_model=" << m_aiModel << "\n";
    ss << "ollama_endpoint=" << m_ollamaEndpoint << "\n";
    ss << "ollama_private_port=" << m_ollamaPrivatePort << "\n";
    if (!m_lmStudioEndpoint.empty()) {
        ss << "lmstudio_endpoint=" << m_lmStudioEndpoint << "\n";
    }
    ss << "\n";
}

void Config::saveCnc(std::ostringstream& ss) const {
    ss << "[cnc]\n";
    ss << "status_poll_interval_ms=" << m_statusPollIntervalMs << "\n";
    ss << "jog_feed_small=" << m_jogFeedSmall << "\n";
    ss << "jog_feed_medium=" << m_jogFeedMedium << "\n";
    ss << "jog_feed_large=" << m_jogFeedLarge << "\n";
    ss << "job_completion_notify=" << (m_jobCompletionNotify ? "true" : "false") << "\n";
    ss << "job_completion_flash=" << (m_jobCompletionFlash ? "true" : "false") << "\n";
    ss << "display_units=" << (m_displayUnitsMetric ? "mm" : "in") << "\n";
    ss << "advanced_settings_view=" << (m_advancedSettingsView ? "true" : "false") << "\n";
    ss << "show_tool_dot=" << (m_cncShowToolDot ? "true" : "false") << "\n";
    ss << "show_work_envelope=" << (m_cncShowWorkEnvelope ? "true" : "false") << "\n";
    ss << "show_dro_overlay=" << (m_cncShowDroOverlay ? "true" : "false") << "\n";
    ss << "tool_dot_size=" << m_cncToolDotSize << "\n";
    ss << "tool_dot_color_r=" << m_cncToolDotColor.x << "\n";
    ss << "tool_dot_color_g=" << m_cncToolDotColor.y << "\n";
    ss << "tool_dot_color_b=" << m_cncToolDotColor.z << "\n";
    ss << "tool_dot_color_a=" << m_cncToolDotColor.w << "\n";
    ss << "envelope_color_r=" << m_cncEnvelopeColor.x << "\n";
    ss << "envelope_color_g=" << m_cncEnvelopeColor.y << "\n";
    ss << "envelope_color_b=" << m_cncEnvelopeColor.z << "\n";
    ss << "envelope_color_a=" << m_cncEnvelopeColor.w << "\n";
    ss << "show_model_bounds=" << (m_cncShowModelBounds ? "true" : "false") << "\n";
    ss << "model_bounds_in_color_r=" << m_cncModelBoundsInColor.x << "\n";
    ss << "model_bounds_in_color_g=" << m_cncModelBoundsInColor.y << "\n";
    ss << "model_bounds_in_color_b=" << m_cncModelBoundsInColor.z << "\n";
    ss << "model_bounds_in_color_a=" << m_cncModelBoundsInColor.w << "\n";
    ss << "model_bounds_out_color_r=" << m_cncModelBoundsOutColor.x << "\n";
    ss << "model_bounds_out_color_g=" << m_cncModelBoundsOutColor.y << "\n";
    ss << "model_bounds_out_color_b=" << m_cncModelBoundsOutColor.z << "\n";
    ss << "model_bounds_out_color_a=" << m_cncModelBoundsOutColor.w << "\n";
    ss << "\n";
}

void Config::saveWcsAliases(std::ostringstream& ss) const {
    ss << "[wcs_aliases]\n";
    for (int i = 0; i < NUM_WCS; ++i) {
        if (!m_wcsAliases[i].empty())
            ss << "g" << (54 + i) << "=" << m_wcsAliases[i] << "\n";
    }
    ss << "\n";
}

void Config::saveRecentGCode(std::ostringstream& ss) const {
    ss << "[recent_gcode]\n";
    for (size_t i = 0; i < m_recentGCodeFiles.size(); ++i) {
        ss << "file" << i << "=" << m_recentGCodeFiles[i].string() << "\n";
    }
    ss << "\n";
}

void Config::saveSafety(std::ostringstream& ss) const {
    ss << "[safety]\n";
    ss << "long_press_enabled=" << (m_safetyLongPressEnabled ? "true" : "false") << "\n";
    ss << "long_press_duration_ms=" << m_safetyLongPressDurationMs << "\n";
    ss << "abort_long_press=" << (m_safetyAbortLongPress ? "true" : "false") << "\n";
    ss << "dead_man_enabled=" << (m_safetyDeadManEnabled ? "true" : "false") << "\n";
    ss << "dead_man_timeout_ms=" << m_safetyDeadManTimeoutMs << "\n";
    ss << "door_interlock_enabled=" << (m_safetyDoorInterlockEnabled ? "true" : "false") << "\n";
    ss << "soft_limit_check_enabled=" << (m_safetySoftLimitCheckEnabled ? "true" : "false") << "\n";
    ss << "pause_before_reset_enabled=" << (m_safetyPauseBeforeResetEnabled ? "true" : "false") << "\n";
    ss << "\n";
}

void Config::saveMachineProfiles(std::ostringstream& ss) const {
    ss << "[machine_profiles]\n";
    ss << "active_profile=" << m_machineProfiles[static_cast<size_t>(m_activeMachineProfileIndex)].name << "\n";
    int userIdx = 0;
    for (const auto& prof : m_machineProfiles) {
        if (!prof.builtIn)
            ss << "profile" << userIdx++ << "=" << prof.toJsonString() << "\n";
    }
    ss << "\n";
}

// ---------------------------------------------------------------------------
// Remaining methods (unchanged)
// ---------------------------------------------------------------------------

void Config::addRecentGCodeFile(const Path& path) {
    const Path durablePath = durableRecentGCodeLocation(path);
    if (durablePath.empty())
        return;
    // Remove if already present
    m_recentGCodeFiles.erase(
        std::remove_if(
            m_recentGCodeFiles.begin(),
            m_recentGCodeFiles.end(),
            [&durablePath](const Path& recent) {
                return durableRecentGCodeLocation(recent) == durablePath;
            }),
        m_recentGCodeFiles.end());
    // Insert at front
    m_recentGCodeFiles.insert(m_recentGCodeFiles.begin(), durablePath);
    // Trim to max
    if (static_cast<int>(m_recentGCodeFiles.size()) > MAX_RECENT_GCODE)
        m_recentGCodeFiles.resize(MAX_RECENT_GCODE);
}

void Config::clearRecentGCodeFiles() {
    m_recentGCodeFiles.clear();
}

void Config::initDefaultBindings() {
    for (int i = 0; i < static_cast<int>(BindAction::COUNT); ++i) {
        m_bindings[static_cast<size_t>(i)] = defaultBinding(static_cast<BindAction>(i));
    }
}

InputBinding Config::getBinding(BindAction action) const {
    int idx = static_cast<int>(action);
    if (idx >= 0 && idx < static_cast<int>(BindAction::COUNT)) {
        return m_bindings[static_cast<size_t>(idx)];
    }
    return {};
}

void Config::setBinding(BindAction action, const InputBinding& binding) {
    int idx = static_cast<int>(action);
    if (idx >= 0 && idx < static_cast<int>(BindAction::COUNT)) {
        m_bindings[static_cast<size_t>(idx)] = binding;
    }
}

void Config::setActiveMachineProfileIndex(int index) {
    if (index >= 0 && index < static_cast<int>(m_machineProfiles.size()))
        m_activeMachineProfileIndex = index;
}

const gcode::MachineProfile& Config::getActiveMachineProfile() const {
    return m_machineProfiles[static_cast<size_t>(m_activeMachineProfileIndex)];
}

void Config::addMachineProfile(const gcode::MachineProfile& profile) {
    m_machineProfiles.push_back(profile);
}

void Config::removeMachineProfile(int index) {
    if (index < 0 || index >= static_cast<int>(m_machineProfiles.size()))
        return;
    if (m_machineProfiles[static_cast<size_t>(index)].builtIn)
        return;
    m_machineProfiles.erase(m_machineProfiles.begin() + index);
    if (m_activeMachineProfileIndex >= static_cast<int>(m_machineProfiles.size()))
        m_activeMachineProfileIndex = static_cast<int>(m_machineProfiles.size()) - 1;
}

void Config::updateMachineProfile(int index, const gcode::MachineProfile& profile) {
    if (index >= 0 && index < static_cast<int>(m_machineProfiles.size()))
        m_machineProfiles[static_cast<size_t>(index)] = profile;
}

Path Config::getEffectiveWorkspaceRoot() const {
    return m_workspaceRoot.empty() ? paths::getUserRoot() : m_workspaceRoot;
}

Path Config::getModelsDir() const {
    return m_modelsDir.empty() ? getEffectiveWorkspaceRoot() / "Models" : m_modelsDir;
}

Path Config::getProjectsDir() const {
    return m_projectsDir.empty() ? getEffectiveWorkspaceRoot() / "Projects" : m_projectsDir;
}

Path Config::getMaterialsDir() const {
    return m_materialsDir.empty() ? getEffectiveWorkspaceRoot() / "Materials" : m_materialsDir;
}

Path Config::getGCodeDir() const {
    return m_gcodeDir.empty() ? getEffectiveWorkspaceRoot() / "GCode" : m_gcodeDir;
}

Path Config::getSupportDir() const {
    return m_supportDir.empty() ? getEffectiveWorkspaceRoot() / "Support" : m_supportDir;
}

} // namespace dw
