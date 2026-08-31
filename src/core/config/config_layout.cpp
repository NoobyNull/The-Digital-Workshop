#include "core/config/config.h"

#include <utility>

#include "core/utils/string_utils.h"

namespace dw {

NavStyle nextNavStyle(NavStyle style) {
    switch (style) {
    case NavStyle::Default:
        return NavStyle::CAD;
    case NavStyle::CAD:
        return NavStyle::Maya;
    case NavStyle::Maya:
        return NavStyle::Default;
    }
    return NavStyle::Default;
}

const char* navStyleLetter(NavStyle style) {
    switch (style) {
    case NavStyle::Default:
        return "D";
    case NavStyle::CAD:
        return "C";
    case NavStyle::Maya:
        return "M";
    }
    return "D";
}

const char* navStyleName(NavStyle style) {
    switch (style) {
    case NavStyle::Default:
        return "Default";
    case NavStyle::CAD:
        return "CAD";
    case NavStyle::Maya:
        return "Maya";
    }
    return "Default";
}

const char* navStyleControlHint(NavStyle style) {
    switch (style) {
    case NavStyle::Default:
        return "Left orbit\nShift+Left or Middle pan\nRight zoom";
    case NavStyle::CAD:
        return "Middle orbit\nShift+Middle or Right pan\nWheel zoom";
    case NavStyle::Maya:
        return "Alt+Left orbit\nAlt+Middle pan\nAlt+Right zoom";
    }
    return "Left orbit\nShift+Left or Middle pan\nRight zoom";
}

void Config::loadUi(const std::string& key, const std::string& value) {
    if (key == "theme") {
        str::parseInt(value, m_themeIndex);
    } else if (key == "scale") {
        str::parseFloat(value, m_uiScale);
    } else if (key == "show_grid") {
        m_showGrid = (value == "true" || value == "1");
    } else if (key == "show_axis") {
        m_showAxis = (value == "true" || value == "1");
    } else if (key == "auto_orient") {
        m_autoOrient = (value == "true" || value == "1");
    } else if (key == "invert_orbit_x") {
        m_invertOrbitX = (value == "true" || value == "1");
    } else if (key == "invert_orbit_y") {
        m_invertOrbitY = (value == "true" || value == "1");
    } else if (key == "nav_style") {
        int style = 0;
        str::parseInt(value, style);
        if (style >= 0 && style <= 2)
            m_navStyle = static_cast<NavStyle>(style);
    } else if (key == "floating_windows") {
        m_enableFloatingWindows = (value == "true" || value == "1");
    }
}

void Config::loadWindow(const std::string& key, const std::string& value) {
    if (key == "width") {
        str::parseInt(value, m_windowWidth);
    } else if (key == "height") {
        str::parseInt(value, m_windowHeight);
    } else if (key == "maximized") {
        m_windowMaximized = (value == "true" || value == "1");
    }
}

void Config::loadWorkspace(const std::string& key, const std::string& value) {
    const bool enabled = value == "true" || value == "1";
    if (key == "show_viewport")
        m_wsShowViewport = enabled;
    else if (key == "show_library")
        m_wsShowLibrary = enabled;
    else if (key == "show_properties")
        m_wsShowProperties = enabled;
    else if (key == "show_project")
        m_wsShowProject = enabled;
    else if (key == "show_materials")
        m_wsShowMaterials = enabled;
    else if (key == "show_gcode")
        m_wsShowGCode = enabled;
    else if (key == "show_cut_optimizer")
        m_wsShowCutOptimizer = enabled;
    else if (key == "show_project_costing" || key == "show_cost_estimator")
        m_wsShowProjectCosting = enabled;
    else if (key == "show_tool_browser")
        m_wsShowToolBrowser = enabled;
    else if (key == "show_cnc_status")
        m_wsShowCncStatus = enabled;
    else if (key == "show_cnc_jog")
        m_wsShowCncJog = enabled;
    else if (key == "show_cnc_console")
        m_wsShowCncConsole = enabled;
    else if (key == "show_cnc_wcs")
        m_wsShowCncWcs = enabled;
    else if (key == "show_cnc_tool")
        m_wsShowCncTool = enabled;
    else if (key == "show_cnc_job")
        m_wsShowCncJob = enabled;
    else if (key == "show_cnc_safety")
        m_wsShowCncSafety = enabled;
    else if (key == "show_cnc_settings")
        m_wsShowCncSettings = enabled;
    else if (key == "show_cnc_macros")
        m_wsShowCncMacros = enabled;
    else if (key == "show_direct_carve")
        m_wsShowDirectCarve = enabled;
    else if (key == "show_start_page")
        m_wsShowStartPage = enabled;
    else if (key == "last_selected_model")
        str::parseInt64(value, m_wsLastSelectedModelId);
    else if (key == "library_thumb_size")
        str::parseFloat(value, m_wsLibraryThumbSize);
    else if (key == "materials_thumb_size")
        str::parseFloat(value, m_wsMaterialsThumbSize);
}

void Config::loadLayoutPresets(const std::string& key,
                               const std::string& value,
                               std::optional<std::vector<LayoutPreset>>& loaded) {
    if (key == "version") {
        str::parseInt(value, m_layoutMigrationVersion);
    } else if (key == "active_preset") {
        str::parseInt(value, m_activeLayoutPresetIndex);
    } else if (str::startsWith(key, "preset")) {
        auto preset = LayoutPreset::fromJsonString(value);
        if (!preset.name.empty()) {
            if (!loaded)
                loaded = std::vector<LayoutPreset>{};
            loaded->push_back(std::move(preset));
        }
    }
}

void Config::resolveLoadedLayoutPresets(std::optional<std::vector<LayoutPreset>>& loaded) {
    const auto result = migrateGuidedLayouts({
        m_layoutMigrationVersion,
        m_activeLayoutPresetIndex,
        loaded && !loaded->empty() ? *loaded : m_layoutPresets,
    });
    m_layoutMigrationVersion = result.version;
    m_activeLayoutPresetIndex = result.activePresetIndex;
    m_layoutPresets = result.presets;

    if (m_activeLayoutPresetIndex < 0 ||
        m_activeLayoutPresetIndex >= static_cast<int>(m_layoutPresets.size())) {
        m_activeLayoutPresetIndex = 1;
    }
}

void Config::saveUi(std::ostringstream& ss) const {
    ss << "[ui]\n";
    ss << "theme=" << m_themeIndex << "\n";
    ss << "scale=" << m_uiScale << "\n";
    ss << "show_grid=" << (m_showGrid ? "true" : "false") << "\n";
    ss << "show_axis=" << (m_showAxis ? "true" : "false") << "\n";
    ss << "auto_orient=" << (m_autoOrient ? "true" : "false") << "\n";
    ss << "invert_orbit_x=" << (m_invertOrbitX ? "true" : "false") << "\n";
    ss << "invert_orbit_y=" << (m_invertOrbitY ? "true" : "false") << "\n";
    ss << "nav_style=" << static_cast<int>(m_navStyle) << "\n";
    ss << "floating_windows=" << (m_enableFloatingWindows ? "true" : "false") << "\n\n";
}

void Config::saveWindow(std::ostringstream& ss) const {
    ss << "[window]\n";
    ss << "width=" << m_windowWidth << "\n";
    ss << "height=" << m_windowHeight << "\n";
    ss << "maximized=" << (m_windowMaximized ? "true" : "false") << "\n\n";
}

void Config::saveWorkspace(std::ostringstream& ss) const {
    ss << "[workspace]\n";
    ss << "show_viewport=" << (m_wsShowViewport ? "true" : "false") << "\n";
    ss << "show_library=" << (m_wsShowLibrary ? "true" : "false") << "\n";
    ss << "show_properties=" << (m_wsShowProperties ? "true" : "false") << "\n";
    ss << "show_project=" << (m_wsShowProject ? "true" : "false") << "\n";
    ss << "show_materials=" << (m_wsShowMaterials ? "true" : "false") << "\n";
    ss << "show_gcode=" << (m_wsShowGCode ? "true" : "false") << "\n";
    ss << "show_cut_optimizer=" << (m_wsShowCutOptimizer ? "true" : "false") << "\n";
    ss << "show_project_costing=" << (m_wsShowProjectCosting ? "true" : "false") << "\n";
    ss << "show_tool_browser=" << (m_wsShowToolBrowser ? "true" : "false") << "\n";
    ss << "show_cnc_status=" << (m_wsShowCncStatus ? "true" : "false") << "\n";
    ss << "show_cnc_jog=" << (m_wsShowCncJog ? "true" : "false") << "\n";
    ss << "show_cnc_console=" << (m_wsShowCncConsole ? "true" : "false") << "\n";
    ss << "show_cnc_wcs=" << (m_wsShowCncWcs ? "true" : "false") << "\n";
    ss << "show_cnc_tool=" << (m_wsShowCncTool ? "true" : "false") << "\n";
    ss << "show_cnc_job=" << (m_wsShowCncJob ? "true" : "false") << "\n";
    ss << "show_cnc_safety=" << (m_wsShowCncSafety ? "true" : "false") << "\n";
    ss << "show_cnc_settings=" << (m_wsShowCncSettings ? "true" : "false") << "\n";
    ss << "show_cnc_macros=" << (m_wsShowCncMacros ? "true" : "false") << "\n";
    ss << "show_direct_carve=" << (m_wsShowDirectCarve ? "true" : "false") << "\n";
    ss << "show_start_page=" << (m_wsShowStartPage ? "true" : "false") << "\n";
    ss << "last_selected_model=" << m_wsLastSelectedModelId << "\n";
    ss << "library_thumb_size=" << m_wsLibraryThumbSize << "\n";
    ss << "materials_thumb_size=" << m_wsMaterialsThumbSize << "\n\n";
}

void Config::saveLayoutPresets(std::ostringstream& ss) const {
    ss << "[layout_presets]\n";
    ss << "version=" << m_layoutMigrationVersion << "\n";
    ss << "active_preset=" << m_activeLayoutPresetIndex << "\n";
    for (std::size_t i = 0; i < m_layoutPresets.size(); ++i)
        ss << "preset" << i << "=" << m_layoutPresets[i].toJsonString() << "\n";
}

void Config::setActiveLayoutPresetIndex(int index) {
    if (index >= 0 && index < static_cast<int>(m_layoutPresets.size()))
        m_activeLayoutPresetIndex = index;
}

void Config::addLayoutPreset(const LayoutPreset& preset) {
    m_layoutPresets.push_back(preset);
}

void Config::removeLayoutPreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_layoutPresets.size()))
        return;
    if (m_layoutPresets[static_cast<std::size_t>(index)].builtIn)
        return;
    m_layoutPresets.erase(m_layoutPresets.begin() + index);
    if (m_activeLayoutPresetIndex >= static_cast<int>(m_layoutPresets.size()))
        m_activeLayoutPresetIndex = static_cast<int>(m_layoutPresets.size()) - 1;
}

void Config::updateLayoutPreset(int index, const LayoutPreset& preset) {
    if (index >= 0 && index < static_cast<int>(m_layoutPresets.size()))
        m_layoutPresets[static_cast<std::size_t>(index)] = preset;
}

} // namespace dw
