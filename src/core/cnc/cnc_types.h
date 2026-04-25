#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../types.h"

namespace dw {

// GRBL machine state
enum class MachineState {
    Unknown,
    Idle,
    Run,
    Hold,
    Jog,
    Alarm,
    Door,
    Check,
    Home,
    Sleep
};

// Parsed GRBL status report from `?` query
struct MachineStatus {
    MachineState state = MachineState::Unknown;
    Vec3 machinePos{0.0f};
    Vec3 workPos{0.0f};
    f32 feedRate = 0.0f;
    f32 spindleSpeed = 0.0f;
    int feedOverride = 100;
    int rapidOverride = 100;
    int spindleOverride = 100;
    u32 inputPins = 0;
};

// Work coordinate system offsets parsed from $# response
struct WcsOffsets {
    Vec3 g54{0.0f};
    Vec3 g55{0.0f};
    Vec3 g56{0.0f};
    Vec3 g57{0.0f};
    Vec3 g58{0.0f};
    Vec3 g59{0.0f};
    Vec3 g28{0.0f};  // G28 home position
    Vec3 g30{0.0f};  // G30 home position
    Vec3 g92{0.0f};  // G92 offset
    float tlo = 0.0f; // Tool length offset

    // Get offset by WCS index (0=G54, 1=G55, ..., 5=G59)
    Vec3 getByIndex(int idx) const {
        switch (idx) {
        case 0: return g54;
        case 1: return g55;
        case 2: return g56;
        case 3: return g57;
        case 4: return g58;
        case 5: return g59;
        default: return Vec3{0.0f};
        }
    }
};

// Result of sending a single line
struct LineAck {
    int lineIndex = -1;      // Index in the gcode program
    bool ok = false;          // true = "ok", false = "error:N"
    int errorCode = 0;        // GRBL error code (0 if ok)
    std::string errorMessage; // Human-readable error description
};

// Streaming progress
struct StreamProgress {
    int totalLines = 0;
    int ackedLines = 0;
    int errorCount = 0;
    f32 elapsedSeconds = 0.0f;
    bool streaming = false;
};

// Detailed streaming error report (when error occurs during character-counting streaming)
struct StreamingError {
    int lineIndex = -1;           // Which program line failed
    int errorCode = 0;            // GRBL error code
    std::string errorMessage;     // Human-readable description
    std::string failedLine;       // The actual G-code line that failed
    int linesInFlight = 0;        // How many lines were buffered in GRBL when error occurred
};

// GRBL alarm descriptions (detailed)
inline const char* alarmDescription(int code) {
    switch (code) {
    case 1: return "Hard limit triggered. Machine position lost -- re-home required";
    case 2: return "G-code motion target exceeds machine travel (soft limit)";
    case 3: return "Reset while in motion. Machine position may be lost -- re-home recommended";
    case 4: return "Probe fail. Probe not contacted within search distance";
    case 5: return "Probe fail. Probe already triggered before starting cycle";
    case 6: return "Homing fail. Reset during homing cycle";
    case 7: return "Homing fail. Safety door opened during homing";
    case 8: return "Homing fail. Cycle failed to clear limit switch -- check wiring and pull-off ($27)";
    case 9: return "Homing fail. Could not find limit switch -- check wiring and max travel ($130-$132)";
    case 10: return "Homing fail. Second dual-axis limit not found";
    default: return "Unknown alarm";
    }
}

// GRBL error descriptions (complete: codes 1-37)
inline const char* errorDescription(int code) {
    switch (code) {
    case 1: return "G-code word consists of a letter with no value";
    case 2: return "Numeric value format is not valid or missing expected value";
    case 3: return "Grbl '$' system command was not recognized or supported";
    case 4: return "Negative value received for an expected positive value";
    case 5: return "Homing cycle is not enabled via settings";
    case 6: return "Minimum step pulse time must be greater than 3usec";
    case 7: return "EEPROM read failed. Reset and restored to default values";
    case 8: return "Grbl '$' command cannot be used unless Grbl is IDLE";
    case 9: return "G-code locked out during alarm or jog state";
    case 10: return "Homing enabled, soft limits require homing before operation";
    case 11: return "Max characters per line exceeded";
    case 12: return "Grbl '$' setting value exceeds the maximum step rate supported";
    case 13: return "Safety door detected as opened and door state initiated";
    case 14: return "Build info or startup line exceeds EEPROM line length limit";
    case 15: return "Jog target exceeds machine travel. Jog command has been ignored";
    case 16: return "Jog command with no '=' or has prohibited g-code";
    case 17: return "Laser mode requires PWM output";
    case 20: return "Unsupported or invalid g-code command found in block";
    case 21: return "More than one g-code command from same modal group found in block";
    case 22: return "Feed rate has not yet been set or is undefined";
    case 23: return "G-code command in block requires an integer value";
    case 24: return "Two G-code commands that both require the use of the XYZ axis words were detected";
    case 25: return "A G-code word was repeated in the block";
    case 26: return "A G-code command requires XYZ axis words in the block, but none were found";
    case 27: return "N line number value is not within the valid range of 1-9999999";
    case 28: return "A G-code command was sent, but is missing some required P or L value words";
    case 29: return "Grbl supports six work coordinate systems G54-G59. G59.1-G59.3 are not supported";
    case 30: return "The G53 G-code command requires either a G0 seek or G1 feed motion mode";
    case 31: return "There are unused axis words in the block and G80 motion mode cancel is active";
    case 32: return "A G2 or G3 arc was commanded but there are no XYZ axis words to trace the arc";
    case 33: return "The motion command has an invalid target. G2/G3 arcs are incorrectly defined";
    case 34: return "A G2 or G3 arc with radius definition had a mathematical error computing arc geometry";
    case 35: return "A G2 or G3 arc with offset definition is missing the IJK offset word";
    case 36: return "There are unused, leftover G-code words that aren't used by any command in the block";
    case 37: return "G43.1 dynamic tool length offset cannot apply an offset to an axis other than configured";
    default: return "Unknown error";
    }
}

// Structured alarm reference entry for UI display
struct AlarmEntry {
    int code;
    const char* name;
    const char* description;
};

// Returns the full GRBL alarm reference table
inline const AlarmEntry* alarmReference(int& count) {
    static const AlarmEntry entries[] = {
        {1, "Hard Limit", "Hard limit triggered. Machine position lost."},
        {2, "Soft Limit", "Motion target exceeds machine travel."},
        {3, "Abort", "Reset while in motion. Position may be lost."},
        {4, "Probe Fail", "Probe not contacted within search distance."},
        {5, "Probe Fail", "Probe already triggered before starting cycle."},
        {6, "Homing Fail", "Reset during homing cycle."},
        {7, "Homing Fail", "Safety door opened during homing."},
        {8, "Homing Fail", "Failed to clear limit switch (check $27)."},
        {9, "Homing Fail", "Could not find limit switch (check $130-$132)."},
        {10, "Homing Fail", "Second dual-axis limit not found."},
    };
    count = static_cast<int>(sizeof(entries) / sizeof(entries[0]));
    return entries;
}

// Structured error reference entry for UI display
struct ErrorEntry {
    int code;
    const char* description;
};

// Returns the full GRBL error reference table
inline const ErrorEntry* errorReference(int& count) {
    static const ErrorEntry entries[] = {
        {1, "G-code word consists of a letter with no value"},
        {2, "Numeric value format is not valid or missing expected value"},
        {3, "Grbl '$' system command was not recognized or supported"},
        {4, "Negative value received for an expected positive value"},
        {5, "Homing cycle is not enabled via settings"},
        {6, "Minimum step pulse time must be greater than 3usec"},
        {7, "EEPROM read failed. Reset and restored to default values"},
        {8, "Grbl '$' command cannot be used unless Grbl is IDLE"},
        {9, "G-code locked out during alarm or jog state"},
        {10, "Homing enabled, soft limits require homing before operation"},
        {11, "Max characters per line exceeded"},
        {12, "Setting value exceeds the maximum step rate supported"},
        {13, "Safety door detected as opened and door state initiated"},
        {14, "Build info or startup line exceeds EEPROM line length limit"},
        {15, "Jog target exceeds machine travel"},
        {16, "Jog command with no '=' or has prohibited g-code"},
        {17, "Laser mode requires PWM output"},
        {20, "Unsupported or invalid g-code command found in block"},
        {21, "More than one g-code command from same modal group"},
        {22, "Feed rate has not yet been set or is undefined"},
        {23, "G-code command in block requires an integer value"},
        {24, "Two commands both require the use of XYZ axis words"},
        {25, "A G-code word was repeated in the block"},
        {26, "A command requires XYZ axis words, but none were found"},
        {27, "N line number value is not within valid range 1-9999999"},
        {28, "Command is missing some required P or L value words"},
        {29, "G59.1, G59.2, and G59.3 are not supported"},
        {30, "G53 requires either a G0 seek or G1 feed motion mode"},
        {31, "Unused axis words in the block and G80 cancel is active"},
        {32, "G2/G3 arc commanded but no XYZ axis words to trace the arc"},
        {33, "Motion command has an invalid target or arc definition"},
        {34, "G2/G3 arc with radius definition had a math error"},
        {35, "G2/G3 arc with offset definition is missing IJK offset word"},
        {36, "Unused, leftover G-code words not used by any command"},
        {37, "G43.1 cannot apply offset to an axis other than configured"},
    };
    count = static_cast<int>(sizeof(entries) / sizeof(entries[0]));
    return entries;
}

// GRBL real-time command bytes
namespace cnc {

constexpr u8 CMD_SOFT_RESET = 0x18;
constexpr u8 CMD_STATUS_QUERY = '?';
constexpr u8 CMD_CYCLE_START = '~';
constexpr u8 CMD_FEED_HOLD = '!';

// Feed override
constexpr u8 CMD_FEED_100_PERCENT = 0x90;
constexpr u8 CMD_FEED_PLUS_10 = 0x91;
constexpr u8 CMD_FEED_MINUS_10 = 0x92;
constexpr u8 CMD_FEED_PLUS_1 = 0x93;
constexpr u8 CMD_FEED_MINUS_1 = 0x94;

// Rapid override
constexpr u8 CMD_RAPID_100_PERCENT = 0x95;
constexpr u8 CMD_RAPID_50_PERCENT = 0x96;
constexpr u8 CMD_RAPID_25_PERCENT = 0x97;

// Spindle override
constexpr u8 CMD_SPINDLE_100_PERCENT = 0x99;
constexpr u8 CMD_SPINDLE_PLUS_10 = 0x9A;
constexpr u8 CMD_SPINDLE_MINUS_10 = 0x9B;
constexpr u8 CMD_SPINDLE_PLUS_1 = 0x9C;
constexpr u8 CMD_SPINDLE_MINUS_1 = 0x9D;

constexpr int RX_BUFFER_SIZE = 128; // GRBL serial RX buffer

// Input pin state bitmask constants (from GRBL Pn: field)
constexpr u32 PIN_X_LIMIT = 1 << 0;
constexpr u32 PIN_Y_LIMIT = 1 << 1;
constexpr u32 PIN_Z_LIMIT = 1 << 2;
constexpr u32 PIN_PROBE   = 1 << 3;
constexpr u32 PIN_DOOR    = 1 << 4;
constexpr u32 PIN_HOLD    = 1 << 5;
constexpr u32 PIN_RESET   = 1 << 6;
constexpr u32 PIN_START   = 1 << 7;

} // namespace cnc

// Callbacks for CncController events (all called on main thread via MainThreadQueue)
struct CncCallbacks {
    std::function<void(bool connected, const std::string& version)> onConnectionChanged;
    std::function<void(const MachineStatus& status)> onStatusUpdate;
    std::function<void(const LineAck& ack)> onLineAcked;
    std::function<void(const StreamProgress& progress)> onProgressUpdate;
    std::function<void(int alarmCode, const std::string& desc)> onAlarm;
    std::function<void(const std::string& message)> onError;
    std::function<void(const StreamingError& error)> onStreamingError;
    std::function<void(const std::string& line, bool isSent)> onRawLine;
    std::function<void(int toolNumber)> onToolChange; // M6 detected during streaming
};

} // namespace dw
