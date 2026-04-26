#include "core/config/window_catalog.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace dw {

bool WindowCatalogEntry::hasLegacyKey(std::string_view candidate) const {
    return std::any_of(legacyKeys.begin(), legacyKeys.end(), [candidate](const auto& legacyKey) {
        return legacyKey == candidate;
    });
}

bool WindowCatalogEntry::matchesKey(std::string_view candidate) const {
    return key == candidate || layoutKey == candidate || hasLegacyKey(candidate);
}

const std::vector<WindowCatalogEntry>& windowCatalogEntries() {
    static const std::vector<WindowCatalogEntry> entries = {
        {"start_page",      "start_page",      "Start Page",        "Start Page",
         WindowRole::Workshop, WindowType::DockablePanel, "center", true, false, true, {}},
        {"viewport",        "viewport",        "Viewport",          "Viewport",
         WindowRole::Shared, WindowType::DockablePanel, "center", true, true, true, {}},
        {"library",         "library",         "Library",           "Library",
         WindowRole::Workshop, WindowType::DockablePanel, "left_top", true, false, true, {}},
        {"properties",      "properties",      "Properties",        "Properties",
         WindowRole::Workshop, WindowType::DockablePanel, "right", true, false, true, {}},
        {"project",         "project",         "Project",           "Project",
         WindowRole::Shared, WindowType::DockablePanel, "left_bottom", true, true, true, {}},
        {"gcode_viewer",    "gcode",           "G-code",            "G-code Viewer",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {"gcode"}},
        {"cut_optimizer",   "cut_optimizer",   "Cut Optimizer",     "Cut Optimizer",
         WindowRole::Workshop, WindowType::DockablePanel, "center", false, false, true, {}},
        {"project_costing", "project_costing", "Project Costing",   "Project Costing",
         WindowRole::Workshop, WindowType::DockablePanel, "left_bottom", false, false, true, {}},
        {"materials",       "materials",       "Materials",         "Materials",
         WindowRole::Workshop, WindowType::DockablePanel, "left_bottom", false, false, true, {}},
        {"tool_library",    "tool_browser",    "Tool Library",      "Tool Library",
         WindowRole::Shared, WindowType::DockablePanel, "right", false, false, true,
         {"tool_browser"}},
        {"cnc_status",      "cnc_status",      "CNC Status",        "Status",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"cnc_jog",         "cnc_jog",         "Jog Control",       "Jog Control",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"cnc_console",     "cnc_console",     "MDI Console",       "MDI Console",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"cnc_wcs",         "cnc_wcs",         "WCS",               "Work Zero / WCS",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"runtime_tool_setup", "cnc_tool",     "Tool & Material",   "Tool & Material",
         WindowRole::Sender, WindowType::DockablePanel, "right", false, true, true,
         {"cnc_tool"}},
        {"cnc_job",         "cnc_job",         "Job Progress",      "Job Progress",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"cnc_safety",      "cnc_safety",      "Safety",            "Safety Controls",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"machine_settings", "cnc_settings",   "Machine Settings",  "Machine Settings",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true,
         {"cnc_settings", "firmware_settings"}},
        {"cnc_macros",      "cnc_macros",      "Macros",            "Macros",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},
        {"direct_carve",    "direct_carve",    "Direct Carve",      "Direct Carve",
         WindowRole::Sender, WindowType::DockablePanel, "center", false, true, true, {}},

        {"file_dialog",     "",                "File Dialog",       "File Dialog",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"import_options",  "",                "Import Options",    "Import Options",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"import_summary",  "",                "Import Complete",   "Import Complete",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"progress",        "",                "Progress",          "Progress",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"lighting_settings", "",              "Lighting Settings", "Lighting Settings",
         WindowRole::Global, WindowType::FloatingService, "floating", false, false, false, {}},
        {"machine_profiles", "",               "Machine Profiles",  "Machine Profiles",
         WindowRole::Global, WindowType::FloatingService, "floating", false, false, false,
         {"machine_editor"}},
        {"maintenance",     "",                "Library Maintenance", "Library Maintenance",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"settings_import", "",                "Import Settings",   "Import Settings",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"tag_image",       "",                "Tag Image",         "Tag Image",
         WindowRole::Global, WindowType::FloatingService, "floating", false, false, false, {}},
        {"tagger_shutdown", "",                "Tagging In Progress", "Tagging In Progress",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"about",           "",                "About Digital Workshop", "About Digital Workshop",
         WindowRole::Global, WindowType::ModalDialog, "modal", false, false, false, {}},
        {"restart_required", "",               "Restart Required",  "Restart Required",
         WindowRole::Global, WindowType::FloatingService, "floating", false, false, false, {}},
        {"status_bar",      "",                "Status Bar",        "Status Bar",
         WindowRole::Shell, WindowType::ShellWidget, "bottom_shell", true, true, false, {}},
        {"toasts",          "",                "Toasts",            "Toasts",
         WindowRole::Shell, WindowType::ShellWidget, "background", true, true, false, {}},
    };
    return entries;
}

const WindowCatalogEntry* findWindowCatalogEntry(std::string_view key) {
    const auto& entries = windowCatalogEntries();
    auto it = std::find_if(entries.begin(), entries.end(), [key](const auto& entry) {
        return entry.matchesKey(key);
    });
    return it == entries.end() ? nullptr : &*it;
}

std::string canonicalWindowKey(std::string_view key) {
    if (const auto* entry = findWindowCatalogEntry(key))
        return entry->key;
    return std::string(key);
}

std::string windowRoleName(WindowRole role) {
    switch (role) {
    case WindowRole::Shared:   return "shared";
    case WindowRole::Workshop: return "workshop";
    case WindowRole::Sender:   return "sender";
    case WindowRole::Global:   return "global";
    case WindowRole::Shell:    return "shell";
    case WindowRole::Local:    return "local";
    }
    return "global";
}

std::string windowTypeName(WindowType type) {
    switch (type) {
    case WindowType::DockablePanel:   return "dockable_panel";
    case WindowType::ModalDialog:     return "modal_dialog";
    case WindowType::FloatingService: return "floating_service";
    case WindowType::ShellWidget:     return "shell_widget";
    case WindowType::LocalPopupOwner: return "local_popup_owner";
    }
    return "dockable_panel";
}

std::string windowCatalogJson() {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : windowCatalogEntries()) {
        entries.push_back({
            {"key", entry.key},
            {"layoutKey", entry.layoutKey},
            {"title", entry.title},
            {"menuLabel", entry.menuLabel},
            {"role", windowRoleName(entry.role)},
            {"type", windowTypeName(entry.type)},
            {"defaultDock", entry.defaultDock},
            {"workshopDefaultVisible", entry.workshopDefaultVisible},
            {"senderDefaultVisible", entry.senderDefaultVisible},
            {"layoutPersistent", entry.layoutPersistent},
            {"legacyKeys", entry.legacyKeys},
        });
    }
    return entries.dump();
}

} // namespace dw
