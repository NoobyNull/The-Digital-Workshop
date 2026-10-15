#include "main_tabs.h"

namespace dw::workshop {

const char* mainTabLabel(MainTab tab) noexcept {
    switch (tab) {
    case MainTab::Home:
        return "Home";
    case MainTab::Library:
        return "Library";
    case MainTab::Prepare:
        return "Prepare";
    case MainTab::Preview:
        return "Preview";
    case MainTab::Device:
        return "Device";
    case MainTab::Project:
        return "Project";
    }
    return "Home";
}

MainTabBlock mainTabBlock(MainTab target, const MainTabFacts& facts) noexcept {
    // A running job holds the machine workspace until it ends, so the controls
    // that stop it can never scroll out of reach.
    if (facts.machineStreaming && !isMachineTab(target))
        return MainTabBlock::MachineRunning;
    return MainTabBlock::None;
}

const char* primaryActionLabel(PrimaryAction action) noexcept {
    switch (action) {
    case PrimaryAction::GenerateToolpaths:
        return "Generate";
    case PrimaryAction::SendToMachine:
        return "Send to Machine";
    }
    return "";
}

PrimaryActionBlock primaryActionBlock(PrimaryAction action,
                                      const MainTabFacts& facts) noexcept {
    switch (action) {
    case PrimaryAction::GenerateToolpaths:
        if (!facts.toolpathGenerationAvailable)
            return PrimaryActionBlock::ToolpathGenerationUnavailable;
        return facts.machineStreaming ? PrimaryActionBlock::MachineRunning
                                      : PrimaryActionBlock::None;
    case PrimaryAction::SendToMachine:
        if (facts.machineStreaming)
            return PrimaryActionBlock::MachineRunning;
        return facts.gcodeLoaded ? PrimaryActionBlock::None
                                 : PrimaryActionBlock::NoGCodeLoaded;
    }
    return PrimaryActionBlock::None;
}

const char* blockReason(MainTabBlock block) noexcept {
    switch (block) {
    case MainTabBlock::None:
        return "";
    case MainTabBlock::MachineRunning:
        return "The machine is running a job. Pause or stop it in Device first.";
    }
    return "";
}

const char* blockReason(PrimaryActionBlock block) noexcept {
    switch (block) {
    case PrimaryActionBlock::None:
        return "";
    case PrimaryActionBlock::ToolpathGenerationUnavailable:
        return "Toolpath generation arrives with the CAM rebuild. Import G-code from "
               "another CAM program in the meantime.";
    case PrimaryActionBlock::NoGCodeLoaded:
        return "Open G-code first: pick it in the project, or use Preview to open a file.";
    case PrimaryActionBlock::MachineRunning:
        return "A job is already running. Watch it in Device.";
    }
    return "";
}

} // namespace dw::workshop
