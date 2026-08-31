#pragma once

#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include "../types.h"

namespace dw {
namespace gcode {

// G-code command types
enum class CommandType {
    Unknown,
    // Motion commands
    G0, // Rapid move
    G1, // Linear move
    G2, // Clockwise arc
    G3, // Counter-clockwise arc
    // Unit commands
    G20, // Inches
    G21, // Millimeters
    // Positioning
    G28, // Home
    G90, // Absolute positioning
    G91, // Relative positioning
    G92, // Set position
    // M commands
    M0,  // Program stop
    M1,  // Optional stop
    M2,  // Program end
    M3,  // Spindle on CW
    M4,  // Spindle on CCW
    M5,  // Spindle off
    M6,  // Tool change
    M30, // Program end and rewind
};

// Units
enum class Units { Millimeters, Inches };

// Positioning mode
enum class PositioningMode { Absolute, Relative };

// A single G-code command with parameters
struct Command {
    CommandType type = CommandType::Unknown;
    std::string raw; // Original line text

    // Position parameters (NaN if not specified)
    f32 x = std::numeric_limits<f32>::quiet_NaN();
    f32 y = std::numeric_limits<f32>::quiet_NaN();
    f32 z = std::numeric_limits<f32>::quiet_NaN();

    // Arc parameters
    f32 i = std::numeric_limits<f32>::quiet_NaN(); // Arc center X offset
    f32 j = std::numeric_limits<f32>::quiet_NaN(); // Arc center Y offset
    f32 r = std::numeric_limits<f32>::quiet_NaN(); // Arc radius

    // Feed rate
    f32 f = std::numeric_limits<f32>::quiet_NaN();

    // Spindle speed
    f32 s = std::numeric_limits<f32>::quiet_NaN();

    // Tool number
    int t = -1;

    // Line number in file
    int lineNumber = 0;

    // Check if parameter is set
    bool hasX() const { return !std::isnan(x); }
    bool hasY() const { return !std::isnan(y); }
    bool hasZ() const { return !std::isnan(z); }
    bool hasI() const { return !std::isnan(i); }
    bool hasJ() const { return !std::isnan(j); }
    bool hasR() const { return !std::isnan(r); }
    bool hasF() const { return !std::isnan(f); }
    bool hasS() const { return !std::isnan(s); }
    bool hasT() const { return t >= 0; }

    // Check if this is a motion command
    bool isMotion() const {
        return type == CommandType::G0 || type == CommandType::G1 || type == CommandType::G2 ||
               type == CommandType::G3;
    }
};

// A segment of the toolpath
struct PathSegment {
    Vec3 start;
    Vec3 end;
    bool isRapid = false; // G0 vs G1
    f32 feedRate = 0.0f;
    int lineNumber = 0;
    int toolNumber = 0;   // Active tool at this segment (from T-code)
};

// Parsed G-code program
struct Program {
    std::vector<Command> commands;
    // Canonical machine-space geometry in millimeters, regardless of whether
    // the exact source commands use G20 or G21.
    std::vector<PathSegment> path;

    Units units = Units::Millimeters;
    PositioningMode positioning = PositioningMode::Absolute;

    // Bounds of the toolpath
    Vec3 boundsMin;
    Vec3 boundsMax;
};

// Statistics about the G-code program
struct Statistics {
    f32 totalPathLength = 0.0f;   // Total travel distance
    f32 cuttingPathLength = 0.0f; // G1 distance only (excluding rapids)
    f32 rapidPathLength = 0.0f;   // G0 distance only
    f32 estimatedTime = 0.0f;     // In minutes
    int toolChangeCount = 0;
    int lineCount = 0;
    int commandCount = 0;
    Vec3 boundsMin;
    Vec3 boundsMax;

    // Per-segment time in seconds (parallel to Program::path)
    std::vector<f32> segmentTimes;
};

} // namespace gcode
} // namespace dw
