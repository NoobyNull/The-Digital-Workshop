#include "gcode_parser.h"

#include <cmath>
#include <sstream>

#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace dw {
namespace gcode {

Program Parser::parse(const std::string& content) {
    Program program;
    m_lastError.clear();

    std::istringstream stream(content);
    std::string line;
    int lineNumber = 0;

    Vec3 currentPos{0.0f, 0.0f, 0.0f};
    f32 currentFeedRate = 0.0f;
    int currentTool = 0;
    CommandType modalMotion = CommandType::G0; // G-code modal group 1 (motion)

    while (std::getline(stream, line)) {
        lineNumber++;

        // Trim and skip empty lines/comments
        line = str::trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '(') {
            continue;
        }

        // Remove inline comments
        auto commentPos = line.find(';');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
            line = str::trim(line);
        }

        Command cmd = parseLine(line, lineNumber);
        cmd.raw = line;

        // Modal motion: if line has coordinates but no explicit G/M command,
        // inherit the last motion command (standard G-code modal behavior)
        if (cmd.type == CommandType::Unknown &&
            (cmd.hasX() || cmd.hasY() || cmd.hasZ())) {
            cmd.type = modalMotion;
        }

        // Track modal motion state
        if (cmd.type == CommandType::G0 || cmd.type == CommandType::G1 ||
            cmd.type == CommandType::G2 || cmd.type == CommandType::G3) {
            modalMotion = cmd.type;
        }

        // Handle unit changes
        if (cmd.type == CommandType::G20) {
            program.units = Units::Inches;
        } else if (cmd.type == CommandType::G21) {
            program.units = Units::Millimeters;
        }

        // Handle positioning mode
        if (cmd.type == CommandType::G90) {
            program.positioning = PositioningMode::Absolute;
        } else if (cmd.type == CommandType::G91) {
            program.positioning = PositioningMode::Relative;
        }

        // Track tool changes (T-code)
        if (cmd.t >= 0) {
            currentTool = cmd.t;
        }

        // Program.path is the application's machine-space representation and
        // is always millimeters/mm-min, even when the exact source commands
        // remain expressed in inches for display and sending.
        const f32 unitScale = program.units == Units::Inches ? 25.4f : 1.0f;

        // Update feed rate if specified
        if (cmd.hasF()) {
            currentFeedRate = cmd.f * unitScale;
        }

        // Generate path segments for motion commands
        if (cmd.isMotion()) {
            Vec3 targetPos = currentPos;

            if (program.positioning == PositioningMode::Absolute) {
                if (cmd.hasX())
                    targetPos.x = cmd.x * unitScale;
                if (cmd.hasY())
                    targetPos.y = cmd.y * unitScale;
                if (cmd.hasZ())
                    targetPos.z = cmd.z * unitScale;
            } else {
                if (cmd.hasX())
                    targetPos.x += cmd.x * unitScale;
                if (cmd.hasY())
                    targetPos.y += cmd.y * unitScale;
                if (cmd.hasZ())
                    targetPos.z += cmd.z * unitScale;
            }

            // Lambda to update bounding box
            auto updateBounds = [&](const Vec3& p) {
                if (program.path.empty()) {
                    program.boundsMin = p;
                    program.boundsMax = p;
                } else {
                    program.boundsMin.x = std::min(program.boundsMin.x, p.x);
                    program.boundsMin.y = std::min(program.boundsMin.y, p.y);
                    program.boundsMin.z = std::min(program.boundsMin.z, p.z);
                    program.boundsMax.x = std::max(program.boundsMax.x, p.x);
                    program.boundsMax.y = std::max(program.boundsMax.y, p.y);
                    program.boundsMax.z = std::max(program.boundsMax.z, p.z);
                }
            };

            f32 segFeedRate = cmd.hasF() ? cmd.f * unitScale : currentFeedRate;

            if ((cmd.type == CommandType::G2 || cmd.type == CommandType::G3) &&
                (cmd.hasI() || cmd.hasJ())) {
                // Arc move (G2 = clockwise, G3 = counter-clockwise)
                // Arc center is at current position + (I, J) offset
                f32 iOff = cmd.hasI() ? cmd.i * unitScale : 0.0f;
                f32 jOff = cmd.hasJ() ? cmd.j * unitScale : 0.0f;

                f32 centerX = currentPos.x + iOff;
                f32 centerY = currentPos.y + jOff;

                // Compute start and end angles relative to center
                f32 startAngle = std::atan2(currentPos.y - centerY, currentPos.x - centerX);
                f32 endAngle = std::atan2(targetPos.y - centerY, targetPos.x - centerX);

                // Determine sweep direction
                constexpr f32 PI2 = 2.0f * 3.14159265358979323846f;
                f32 sweep;
                if (cmd.type == CommandType::G2) {
                    // Clockwise: sweep should be negative
                    sweep = endAngle - startAngle;
                    if (sweep >= 0.0f) {
                        sweep -= PI2;
                    }
                } else {
                    // Counter-clockwise: sweep should be positive
                    sweep = endAngle - startAngle;
                    if (sweep <= 0.0f) {
                        sweep += PI2;
                    }
                }

                // Radius from start point (use this for the arc)
                f32 radius = std::sqrt((currentPos.x - centerX) * (currentPos.x - centerX) +
                                       (currentPos.y - centerY) * (currentPos.y - centerY));

                // Number of segments: ~1 per 5 degrees, max 72 per full circle
                constexpr f32 DEG_PER_SEG = 5.0f * 3.14159265358979323846f / 180.0f;
                int numSegments = static_cast<int>(std::ceil(std::fabs(sweep) / DEG_PER_SEG));
                if (numSegments < 1)
                    numSegments = 1;
                if (numSegments > 72)
                    numSegments = 72;

                f32 angleStep = sweep / static_cast<f32>(numSegments);
                f32 startZ = currentPos.z;
                f32 endZ = targetPos.z;

                Vec3 prevPoint = currentPos;
                updateBounds(prevPoint);

                for (int seg = 1; seg <= numSegments; ++seg) {
                    f32 t = static_cast<f32>(seg) / static_cast<f32>(numSegments);
                    f32 angle = startAngle + angleStep * static_cast<f32>(seg);

                    Vec3 point;
                    if (seg == numSegments) {
                        // Last segment snaps exactly to the target endpoint
                        point = targetPos;
                    } else {
                        point.x = centerX + radius * std::cos(angle);
                        point.y = centerY + radius * std::sin(angle);
                        point.z = startZ + (endZ - startZ) * t;
                    }

                    PathSegment arcSeg;
                    arcSeg.start = prevPoint;
                    arcSeg.end = point;
                    arcSeg.isRapid = false; // Arcs are cutting moves
                    arcSeg.feedRate = segFeedRate;
                    arcSeg.lineNumber = lineNumber;
                    arcSeg.toolNumber = currentTool;

                    program.path.push_back(arcSeg);
                    updateBounds(point);

                    prevPoint = point;
                }

                currentPos = targetPos;
            } else {
                // Linear move (G0 rapid or G1 cutting) or G2/G3 fallback without I/J
                PathSegment segment;
                segment.start = currentPos;
                segment.end = targetPos;
                segment.isRapid = (cmd.type == CommandType::G0);
                segment.feedRate = segFeedRate;
                segment.lineNumber = lineNumber;
                segment.toolNumber = currentTool;

                program.path.push_back(segment);

                updateBounds(currentPos);
                updateBounds(targetPos);

                currentPos = targetPos;
            }
        }

        program.commands.push_back(cmd);
    }

    return program;
}

Program Parser::parseFile(const Path& path) {
    auto content = file::readText(path);
    if (!content) {
        m_lastError = "Failed to read file: " + path.string();
        log::error("GCode", m_lastError);
        return Program{};
    }

    return parse(*content);
}

Command Parser::parseLine(const std::string& line, int lineNumber) {
    Command cmd;
    cmd.lineNumber = lineNumber;

    std::string upper = str::toUpper(line);

    // Parse command code (G or M followed by number)
    for (usize i = 0; i < upper.size(); ++i) {
        char c = upper[i];

        if (c == 'G' || c == 'M') {
            // Find the number following the letter
            usize numStart = i + 1;
            while (numStart < upper.size() && upper[numStart] == ' ') {
                numStart++;
            }

            int number = 0;
            if (numStart < upper.size() && std::isdigit(upper[numStart])) {
                std::string numStr;
                while (numStart < upper.size() && std::isdigit(upper[numStart])) {
                    numStr += upper[numStart];
                    numStart++;
                }
                number = std::stoi(numStr);
            }

            CommandType type = parseCommandType(c, number);
            if (type != CommandType::Unknown) {
                cmd.type = type;
            }
        }
    }

    // Parse parameters
    cmd.x = parseParameter(upper, 'X');
    cmd.y = parseParameter(upper, 'Y');
    cmd.z = parseParameter(upper, 'Z');
    cmd.i = parseParameter(upper, 'I');
    cmd.j = parseParameter(upper, 'J');
    cmd.r = parseParameter(upper, 'R');
    cmd.f = parseParameter(upper, 'F');
    cmd.s = parseParameter(upper, 'S');

    // Parse tool number
    f32 t = parseParameter(upper, 'T');
    if (!std::isnan(t)) {
        cmd.t = static_cast<int>(t);
    }

    return cmd;
}

CommandType Parser::parseCommandType(char letter, int number) {
    if (letter == 'G') {
        switch (number) {
        case 0:
            return CommandType::G0;
        case 1:
            return CommandType::G1;
        case 2:
            return CommandType::G2;
        case 3:
            return CommandType::G3;
        case 20:
            return CommandType::G20;
        case 21:
            return CommandType::G21;
        case 28:
            return CommandType::G28;
        case 90:
            return CommandType::G90;
        case 91:
            return CommandType::G91;
        case 92:
            return CommandType::G92;
        default:
            return CommandType::Unknown;
        }
    } else if (letter == 'M') {
        switch (number) {
        case 0:
            return CommandType::M0;
        case 1:
            return CommandType::M1;
        case 2:
            return CommandType::M2;
        case 3:
            return CommandType::M3;
        case 4:
            return CommandType::M4;
        case 5:
            return CommandType::M5;
        case 6:
            return CommandType::M6;
        case 30:
            return CommandType::M30;
        default:
            return CommandType::Unknown;
        }
    }

    return CommandType::Unknown;
}

f32 Parser::parseParameter(const std::string& line, char param) {
    usize pos = line.find(param);
    if (pos == std::string::npos) {
        return std::numeric_limits<f32>::quiet_NaN();
    }

    pos++; // Move past the parameter letter

    // Skip spaces
    while (pos < line.size() && line[pos] == ' ') {
        pos++;
    }

    if (pos >= line.size()) {
        return std::numeric_limits<f32>::quiet_NaN();
    }

    // Extract number (including sign and decimal)
    std::string numStr;
    if (line[pos] == '-' || line[pos] == '+') {
        numStr += line[pos];
        pos++;
    }

    while (pos < line.size() && (std::isdigit(line[pos]) || line[pos] == '.')) {
        numStr += line[pos];
        pos++;
    }

    if (numStr.empty() || numStr == "-" || numStr == "+") {
        return std::numeric_limits<f32>::quiet_NaN();
    }

    try {
        return std::stof(numStr);
    } catch (...) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
}

} // namespace gcode
} // namespace dw
