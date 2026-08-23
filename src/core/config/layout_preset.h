#pragma once

#include <string>
#include <unordered_map>

namespace dw {

// Named layout preset controlling panel visibility.
// Persisted as JSON strings in config.ini (same pattern as MachineProfile).
struct LayoutPreset {
    // Stable built-in identity. Custom presets leave this empty so their
    // persisted representation remains backward compatible.
    std::string id;
    std::string name;
    bool builtIn = false; // Prevents deletion

    // Panel visibility: key -> visible (20 keys matching UIManager m_show* flags)
    std::unordered_map<std::string, bool> visibility;

    // Auto-context: focusing a panel with this key activates this preset.
    // Empty means no auto-trigger.
    std::string autoTriggerPanelKey;

    // JSON serialization (follows MachineProfile pattern)
    std::string toJsonString() const;
    static LayoutPreset fromJsonString(const std::string& jsonStr);

    // Built-in preset factories
    static LayoutPreset guidedDefault();
    static LayoutPreset advancedDefault();
    // Stable source alias retained for callers that historically requested
    // the modeling/workshop preset.
    static LayoutPreset modelDefault();
    static LayoutPreset cncDefault();
};

inline constexpr const char* GUIDED_LAYOUT_ID = "guided_workshop";
inline constexpr const char* ADVANCED_LAYOUT_ID = "advanced_workbench";
inline constexpr const char* CNC_LAYOUT_ID = "cnc_sender";

// All valid panel keys (for validation and iteration)
inline constexpr const char* PANEL_KEYS[] = {
    "viewport",      "library",     "properties",   "project",
    "gcode",         "cut_optimizer", "project_costing", "materials",
    "tool_browser",  "start_page",
    "cnc_status",    "cnc_jog",     "cnc_console",  "cnc_wcs",
    "cnc_tool",      "cnc_job",     "cnc_safety",   "cnc_settings",
    "cnc_macros",    "direct_carve",
};
inline constexpr int PANEL_KEY_COUNT = 20;

} // namespace dw
