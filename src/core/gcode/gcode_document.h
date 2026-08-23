#pragma once

#include <string>
#include <vector>

#include "gcode_types.h"

namespace dw::gcode {

struct MachineProfile;

// One immutable interpretation of exact G-code text. UI surfaces share this
// result so preview, statistics, persistence metadata, and the eventual saved
// file cannot drift through separate parse/analyze passes.
struct PreparedDocument {
    std::string exactText;
    Program program;
    Statistics statistics;
    std::vector<f32> feedRates;
    std::vector<int> toolNumbers;

    [[nodiscard]] bool hasCommands() const noexcept {
        return !program.commands.empty();
    }

    [[nodiscard]] bool hasMotion() const noexcept {
        return !program.path.empty();
    }
};

[[nodiscard]] PreparedDocument prepareDocument(
    std::string exactText,
    const MachineProfile& machineProfile);

} // namespace dw::gcode
