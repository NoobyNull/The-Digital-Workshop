#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dw {

// A single entry in a context menu
struct ContextMenuEntry {
    std::string label;
    std::function<void()> action;
    std::string icon; // Empty if no icon
    std::function<bool()> enabled = []() { return true; };
    std::function<bool()> visible = []() { return true; };
    bool isSeparator = false;
    std::vector<ContextMenuEntry> submenu; // Empty if not a submenu parent

    ContextMenuEntry() = default;

    ContextMenuEntry(std::string entryLabel,
                     std::function<void()> entryAction,
                     std::string entryIcon = "",
                     std::function<bool()> entryEnabled = []() { return true; },
                     std::function<bool()> entryVisible = []() { return true; })
        : label(std::move(entryLabel)),
          action(std::move(entryAction)),
          icon(std::move(entryIcon)),
          enabled(std::move(entryEnabled)),
          visible(std::move(entryVisible)) {}

    // Constructor for separator
    static ContextMenuEntry separator() {
        ContextMenuEntry entry;
        entry.isSeparator = true;
        return entry;
    }
};

// Central manager for context menus
// Handles ImGui popup lifecycle and rendering of menu entries
class ContextMenuManager {
  public:
    ContextMenuManager();
    ~ContextMenuManager();

    // Register context menu entries for a popup ID
    // Entries are stored and reused across frames
    void registerEntries(const std::string& popupId, const std::vector<ContextMenuEntry>& entries);

    // Render a registered context menu popup (call inside BeginPopup/EndPopup block)
    // Returns true if the popup is currently open
    bool render(const std::string& popupId);

    // Check if a specific popup is open
    bool isPopupOpen(const std::string& popupId) const;

    // Close a popup by ID
    void closePopup(const std::string& popupId);

    // Clear all registered entries for a popup
    void clearEntries(const std::string& popupId);

  private:
    // Recursively render menu entries (handles submenus)
    void renderEntries(const std::vector<ContextMenuEntry>& entries);

    // Storage for registered entries: popupId -> entries
    std::unordered_map<std::string, std::vector<ContextMenuEntry>> m_entries;

    // Track which popups are currently open (ImGui handles actual popup state)
    std::unordered_map<std::string, bool> m_popupState;
};

} // namespace dw
