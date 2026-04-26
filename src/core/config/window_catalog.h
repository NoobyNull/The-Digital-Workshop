#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dw {

enum class WindowRole {
    Shared,
    Workshop,
    Sender,
    Global,
    Shell,
    Local,
};

enum class WindowType {
    DockablePanel,
    ModalDialog,
    FloatingService,
    ShellWidget,
    LocalPopupOwner,
};

struct WindowCatalogEntry {
    std::string key;
    std::string layoutKey;
    std::string title;
    std::string menuLabel;
    WindowRole role = WindowRole::Global;
    WindowType type = WindowType::DockablePanel;
    std::string defaultDock;
    bool workshopDefaultVisible = false;
    bool senderDefaultVisible = false;
    bool layoutPersistent = false;
    std::vector<std::string> legacyKeys;

    [[nodiscard]] bool hasLegacyKey(std::string_view candidate) const;
    [[nodiscard]] bool matchesKey(std::string_view candidate) const;
};

[[nodiscard]] const std::vector<WindowCatalogEntry>& windowCatalogEntries();
[[nodiscard]] const WindowCatalogEntry* findWindowCatalogEntry(std::string_view key);
[[nodiscard]] std::string canonicalWindowKey(std::string_view key);
[[nodiscard]] std::string windowRoleName(WindowRole role);
[[nodiscard]] std::string windowTypeName(WindowType type);
[[nodiscard]] std::string windowCatalogJson();

} // namespace dw

