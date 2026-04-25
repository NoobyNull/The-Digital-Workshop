#include "gcode_modal_scanner.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace dw {

std::vector<std::string> ModalState::toPreamble() const {
    std::vector<std::string> lines;

    // 1. Units (must come first so subsequent values are interpreted correctly)
    lines.push_back(units);

    // 2. Coordinate system
    lines.push_back(coordinateSystem);

    // 3. Distance mode
    lines.push_back(distanceMode);

    // 4. Feed rate (only if set)
    if (feedRate > 0.0f) {
        std::ostringstream oss;
        oss << "F" << feedRate;
        lines.push_back(oss.str());
    }

    // 5. Spindle speed (only if set)
    if (spindleSpeed > 0.0f) {
        std::ostringstream oss;
        oss << "S" << spindleSpeed;
        lines.push_back(oss.str());
    }

    // 6. Spindle state
    lines.push_back(spindleState);

    // 7. Coolant state
    lines.push_back(coolantState);

    return lines;
}

// Strip comments from a G-code line:
// - Remove content between ( and )
// - Remove everything after ;
static std::string stripComments(const std::string& line) {
    std::string result;
    result.reserve(line.size());

    bool inParen = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == ';')
            break; // Rest of line is comment
        if (c == '(') {
            inParen = true;
            continue;
        }
        if (c == ')') {
            inParen = false;
            continue;
        }
        if (!inParen)
            result += c;
    }

    return result;
}

// Convert string to uppercase
static std::string toUpper(const std::string& s) {
    std::string result = s;
    for (auto& c : result)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

ModalState GCodeModalScanner::scanToLine(const std::vector<std::string>& program, int endLine) {
    ModalState state;

    if (endLine <= 0) {
        return state;
    }

    size_t limit = std::min(static_cast<size_t>(endLine), program.size());

    for (size_t i = 0; i < limit; ++i) {
        std::string line = toUpper(stripComments(program[i]));

        if (line.empty())
            continue;

        // Scan through the line character by character to extract codes and values
        size_t pos = 0;
        while (pos < line.size()) {
            // Skip whitespace
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;

            if (pos >= line.size())
                break;

            char letter = line[pos];

            if (letter == 'G') {
                // Extract the number after G
                ++pos;
                // Skip spaces between letter and number
                while (pos < line.size() && line[pos] == ' ')
                    ++pos;
                int num = 0;
                bool hasNum = false;
                while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) {
                    num = num * 10 + (line[pos] - '0');
                    hasNum = true;
                    ++pos;
                }
                // Skip any decimal part (e.g., G28.1)
                if (pos < line.size() && line[pos] == '.') {
                    ++pos;
                    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])))
                        ++pos;
                }

                if (!hasNum)
                    continue;

                switch (num) {
                case 20: state.units = "G20"; break;
                case 21: state.units = "G21"; break;
                case 90: state.distanceMode = "G90"; break;
                case 91: state.distanceMode = "G91"; break;
                case 54: state.coordinateSystem = "G54"; break;
                case 55: state.coordinateSystem = "G55"; break;
                case 56: state.coordinateSystem = "G56"; break;
                case 57: state.coordinateSystem = "G57"; break;
                case 58: state.coordinateSystem = "G58"; break;
                case 59: state.coordinateSystem = "G59"; break;
                default: break; // G0, G1, G2, G3, etc. don't affect modal tracking
                }
            } else if (letter == 'M') {
                ++pos;
                while (pos < line.size() && line[pos] == ' ')
                    ++pos;
                int num = 0;
                bool hasNum = false;
                while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) {
                    num = num * 10 + (line[pos] - '0');
                    hasNum = true;
                    ++pos;
                }

                if (!hasNum)
                    continue;

                switch (num) {
                case 3: state.spindleState = "M3"; break;
                case 4: state.spindleState = "M4"; break;
                case 5: state.spindleState = "M5"; break;
                case 7: state.coolantState = "M7"; break;
                case 8: state.coolantState = "M8"; break;
                case 9: state.coolantState = "M9"; break;
                default: break;
                }
            } else if (letter == 'F') {
                ++pos;
                while (pos < line.size() && line[pos] == ' ')
                    ++pos;
                // Parse float value
                size_t start = pos;
                if (pos < line.size() && (line[pos] == '-' || line[pos] == '+'))
                    ++pos;
                while (pos < line.size() &&
                       (std::isdigit(static_cast<unsigned char>(line[pos])) || line[pos] == '.'))
                    ++pos;
                if (pos > start) {
                    state.feedRate = std::strtof(line.c_str() + start, nullptr);
                }
            } else if (letter == 'S') {
                ++pos;
                while (pos < line.size() && line[pos] == ' ')
                    ++pos;
                size_t start = pos;
                if (pos < line.size() && (line[pos] == '-' || line[pos] == '+'))
                    ++pos;
                while (pos < line.size() &&
                       (std::isdigit(static_cast<unsigned char>(line[pos])) || line[pos] == '.'))
                    ++pos;
                if (pos > start) {
                    state.spindleSpeed = std::strtof(line.c_str() + start, nullptr);
                }
            } else {
                // Skip other letters and their numeric arguments (X, Y, Z, I, J, K, etc.)
                ++pos;
                while (pos < line.size() && line[pos] == ' ')
                    ++pos;
                // Skip number
                if (pos < line.size() && (line[pos] == '-' || line[pos] == '+'))
                    ++pos;
                while (pos < line.size() &&
                       (std::isdigit(static_cast<unsigned char>(line[pos])) || line[pos] == '.'))
                    ++pos;
            }
        }
    }

    return state;
}

} // namespace dw
