#include "gcode_document.h"

#include <algorithm>
#include <set>
#include <utility>

#include "gcode_analyzer.h"
#include "gcode_parser.h"
#include "machine_profile.h"

namespace dw::gcode {

PreparedDocument prepareDocument(std::string exactText,
                                 const MachineProfile& machineProfile) {
    PreparedDocument document;
    document.exactText = std::move(exactText);

    Parser parser;
    document.program = parser.parse(document.exactText);

    Analyzer analyzer;
    analyzer.setMachineProfile(machineProfile);
    document.statistics = analyzer.analyze(document.program);

    std::set<f32> feedRates;
    std::set<int> toolNumbers;
    for (const auto& command : document.program.commands) {
        if (command.hasF()) feedRates.insert(command.f);
        if (command.hasT()) toolNumbers.insert(command.t);
    }
    document.feedRates.assign(feedRates.begin(), feedRates.end());
    document.toolNumbers.assign(toolNumbers.begin(), toolNumbers.end());
    return document;
}

} // namespace dw::gcode
