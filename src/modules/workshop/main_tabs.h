#pragma once

// Slicer-style top-level tabs (Bambu Studio / OrcaSlicer layout). The shell
// renders them; this header decides which tab may be entered and which
// primary action may run, so those rules stay testable without a UI.

#include <array>

namespace dw::workshop {

enum class MainTab {
    Home,
    Library,
    Prepare,
    Preview,
    Device,
    Project,
};

inline constexpr std::array<MainTab, 6> kMainTabs = {
    MainTab::Home,
    MainTab::Library,
    MainTab::Prepare,
    MainTab::Preview,
    MainTab::Device,
    MainTab::Project,
};

[[nodiscard]] const char* mainTabLabel(MainTab tab) noexcept;

// The Device tab is the machine workspace; every other tab is the workshop.
[[nodiscard]] constexpr bool isMachineTab(MainTab tab) noexcept {
    return tab == MainTab::Device;
}

// Facts the shell gathers once per frame.
struct MainTabFacts {
    MainTab active = MainTab::Home;
    bool machineStreaming = false;
    bool gcodeLoaded = false;
    bool toolpathGenerationAvailable = false;
};

enum class MainTabBlock {
    None,
    MachineRunning,
};

[[nodiscard]] MainTabBlock mainTabBlock(MainTab target, const MainTabFacts& facts) noexcept;

// A slicer's "Slice plate" and "Print plate" buttons.
enum class PrimaryAction {
    GenerateToolpaths,
    SendToMachine,
};

enum class PrimaryActionBlock {
    None,
    ToolpathGenerationUnavailable,
    NoGCodeLoaded,
    MachineRunning,
};

[[nodiscard]] const char* primaryActionLabel(PrimaryAction action) noexcept;
[[nodiscard]] PrimaryActionBlock primaryActionBlock(PrimaryAction action,
                                                    const MainTabFacts& facts) noexcept;

// Plain-language explanations shown as tooltips on disabled controls.
[[nodiscard]] const char* blockReason(MainTabBlock block) noexcept;
[[nodiscard]] const char* blockReason(PrimaryActionBlock block) noexcept;

} // namespace dw::workshop
