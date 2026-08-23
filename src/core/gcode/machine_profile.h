#pragma once

#include <string>
#include <vector>

#include "../cnc/unified_settings.h"
#include "../types.h"

namespace dw {
namespace gcode {

enum class ConnectionType { Auto, Serial, TCP };
enum class DriveSystem { Belt, Acme, LeadScrew, BallScrew, Custom };
enum class HomeCorner { BottomLeft, BottomRight, TopLeft, TopRight, Center };

// CNC machine kinematic parameters for accurate motion planning.
// Values map to GRBL $ settings where noted.
struct MachineProfile {
    std::string name = "Default";

    // Per-axis max feed rates (mm/min) — GRBL $110/$111/$112
    f32 maxFeedRateX = 5000.0f;
    f32 maxFeedRateY = 5000.0f;
    f32 maxFeedRateZ = 3000.0f;

    // Per-axis acceleration (mm/s^2) — GRBL $120/$121/$122
    f32 accelX = 200.0f;
    f32 accelY = 200.0f;
    f32 accelZ = 100.0f;

    // Max travel (mm) — GRBL $130/$131/$132
    f32 maxTravelX = 430.0f;
    f32 maxTravelY = 430.0f;
    f32 maxTravelZ = 100.0f;

    // Junction deviation (mm) — GRBL $11 (reserved for future lookahead)
    f32 junctionDeviation = 0.01f;

    // Default rates (mm/min)
    f32 rapidRate = 5000.0f;
    f32 defaultFeedRate = 1000.0f;
    f32 defaultPlungeRate = 500.0f;
    f32 defaultStepdown = 1.0f;

    // Connection preferences
    ConnectionType connectionType = ConnectionType::Auto;
    FirmwareType preferredFirmware = FirmwareType::GRBL;
    int baudRate = 115200;
    std::string tcpHost;
    int tcpPort = 23;

    // Spindle
    f32 spindleMaxRPM = 10000.0f;
    f32 spindlePower = 0.0f;     // watts, 0 = unspecified
    bool spindleReverse = false;  // supports M4

    // Drive system
    DriveSystem driveSystem = DriveSystem::LeadScrew;
    // Conservative 0.10..1.00 recommendation multiplier, used for Custom only.
    f32 customRigidityFactor = 0.80f;

    // Home location — which corner the machine homes to (Center computes midpoint)
    HomeCorner homeCorner = HomeCorner::BottomLeft;

    // Auxiliary capabilities
    bool hasDustCollection = false;
    bool hasCoolant = false;
    bool hasMistCoolant = false;
    bool hasProbe = false;
    bool hasToolChanger = false;
    bool hasToolLengthOffset = false;

    // True for the built-in presets (prevents deletion)
    bool builtIn = false;

    // JSON serialization (returns/accepts JSON string)
    std::string toJsonString() const;
    static MachineProfile fromJsonString(const std::string& jsonStr);

    // Named preset factories
    static MachineProfile defaultProfile();

    // Sienci Labs
    static MachineProfile longmillMK2();
    static MachineProfile longmillMK2_48x30();
    static MachineProfile altmill48();

    // Shapeoko (Carbide 3D)
    static MachineProfile shapeoko4();
    static MachineProfile shapeoko5Pro();

    // OneFinity
    static MachineProfile onefinityWoodworker();
    static MachineProfile onefinityJourneyman();
    static MachineProfile onefinityForeman();

    // FoxAlien
    static MachineProfile foxalienMasuter();
    static MachineProfile foxalienVasto();
    static MachineProfile foxalien8040();

    // Genmitsu (SainSmart)
    static MachineProfile genmitsu3018();
    static MachineProfile genmitsu4030();

    // X-Carve (Inventables)
    static MachineProfile xcarve();

    // BobsCNC
    static MachineProfile bobscncE4();

    // OpenBuilds
    static MachineProfile openbuildsLead1010();
    static MachineProfile openbuildsLead1515();

    // MillRight
    static MachineProfile millrightMegaV();

    // Returns all built-in presets
    static std::vector<MachineProfile> allBuiltInPresets();
};

} // namespace gcode
} // namespace dw
