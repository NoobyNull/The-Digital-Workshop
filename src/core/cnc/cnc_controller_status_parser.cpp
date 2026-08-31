#include "cnc_controller.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace dw {

MachineState CncController::parseState(const std::string& stateStr) {
    if (stateStr == "Idle") return MachineState::Idle;
    if (stateStr == "Run") return MachineState::Run;
    if (stateStr == "Hold" || stateStr.find("Hold:") == 0) return MachineState::Hold;
    if (stateStr == "Jog") return MachineState::Jog;
    if (stateStr == "Alarm") return MachineState::Alarm;
    if (stateStr == "Door" || stateStr.find("Door:") == 0) return MachineState::Door;
    if (stateStr == "Check") return MachineState::Check;
    if (stateStr == "Home") return MachineState::Home;
    if (stateStr == "Sleep") return MachineState::Sleep;
    return MachineState::Unknown;
}

MachineStatus CncController::parseStatusReport(const std::string& report) {
    MachineStatus status;

    // Format: <State|MPos:x,y,z|WPos:x,y,z|FS:feed,speed|Ov:f,r,s|Pn:XYZ>
    if (report.size() < 3)
        return status;

    const std::string inner = report.substr(1, report.size() - 2);
    std::vector<std::string> fields;
    std::istringstream stream(inner);
    std::string field;
    while (std::getline(stream, field, '|'))
        fields.push_back(field);

    if (fields.empty())
        return status;

    status.state = parseState(fields[0]);

    for (size_t i = 1; i < fields.size(); ++i) {
        const auto& current = fields[i];
        const auto colonPos = current.find(':');
        if (colonPos == std::string::npos)
            continue;

        const std::string key = current.substr(0, colonPos);
        const std::string value = current.substr(colonPos + 1);
        const auto parseVec3 = [](const std::string& encoded) -> Vec3 {
            Vec3 result{0.0f};
            if (std::sscanf(encoded.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z) != 3)
                std::sscanf(encoded.c_str(), "%f,%f", &result.x, &result.y);
            return result;
        };

        if (key == "MPos") {
            status.machinePos = parseVec3(value);
        } else if (key == "WPos") {
            status.workPos = parseVec3(value);
        } else if (key == "WCO") {
            const Vec3 workCoordinateOffset = parseVec3(value);
            status.workPos = status.machinePos - workCoordinateOffset;
        } else if (key == "FS" || key == "F") {
            if (key == "FS") {
                std::sscanf(value.c_str(), "%f,%f", &status.feedRate, &status.spindleSpeed);
            } else {
                std::sscanf(value.c_str(), "%f", &status.feedRate);
            }
        } else if (key == "Ov") {
            std::sscanf(value.c_str(), "%d,%d,%d", &status.feedOverride,
                        &status.rapidOverride, &status.spindleOverride);
        } else if (key == "Pn") {
            status.inputPins = 0;
            for (const char pin : value) {
                switch (pin) {
                case 'X': status.inputPins |= cnc::PIN_X_LIMIT; break;
                case 'Y': status.inputPins |= cnc::PIN_Y_LIMIT; break;
                case 'Z': status.inputPins |= cnc::PIN_Z_LIMIT; break;
                case 'P': status.inputPins |= cnc::PIN_PROBE; break;
                case 'D': status.inputPins |= cnc::PIN_DOOR; break;
                case 'H': status.inputPins |= cnc::PIN_HOLD; break;
                case 'R': status.inputPins |= cnc::PIN_RESET; break;
                case 'S': status.inputPins |= cnc::PIN_START; break;
                }
            }
        }
    }

    return status;
}

} // namespace dw
