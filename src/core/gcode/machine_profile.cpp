#include "machine_profile.h"

#include "machine_rigidity.h"

#include <nlohmann/json.hpp>

namespace dw {
namespace gcode {

// --- Enum string helpers ---

static const char* connectionTypeToString(ConnectionType ct) {
    switch (ct) {
        case ConnectionType::Serial: return "Serial";
        case ConnectionType::TCP:    return "TCP";
        default:                     return "Auto";
    }
}

static ConnectionType connectionTypeFromString(const std::string& s) {
    if (s == "Serial") return ConnectionType::Serial;
    if (s == "TCP")    return ConnectionType::TCP;
    return ConnectionType::Auto;
}

static const char* firmwareTypeToString(FirmwareType ft) {
    switch (ft) {
        case FirmwareType::GrblHAL: return "GrblHAL";
        case FirmwareType::FluidNC: return "FluidNC";
        default:                    return "GRBL";
    }
}

static FirmwareType firmwareTypeFromString(const std::string& s) {
    if (s == "GrblHAL") return FirmwareType::GrblHAL;
    if (s == "FluidNC") return FirmwareType::FluidNC;
    return FirmwareType::GRBL;
}

static const char* driveSystemToString(DriveSystem ds) {
    switch (ds) {
        case DriveSystem::Belt:      return "Belt";
        case DriveSystem::Acme:      return "Acme";
        case DriveSystem::BallScrew: return "BallScrew";
        case DriveSystem::Custom:    return "Custom";
        default:                     return "LeadScrew";
    }
}

static DriveSystem driveSystemFromString(const std::string& s) {
    if (s == "Belt")      return DriveSystem::Belt;
    if (s == "Acme")      return DriveSystem::Acme;
    if (s == "BallScrew") return DriveSystem::BallScrew;
    if (s == "Custom")    return DriveSystem::Custom;
    return DriveSystem::LeadScrew;
}

static const char* homeCornerToString(HomeCorner hc) {
    switch (hc) {
        case HomeCorner::BottomRight: return "BottomRight";
        case HomeCorner::TopLeft:     return "TopLeft";
        case HomeCorner::TopRight:    return "TopRight";
        case HomeCorner::Center:      return "Center";
        default:                      return "BottomLeft";
    }
}

static HomeCorner homeCornerFromString(const std::string& s) {
    if (s == "BottomRight") return HomeCorner::BottomRight;
    if (s == "TopLeft")     return HomeCorner::TopLeft;
    if (s == "TopRight")    return HomeCorner::TopRight;
    if (s == "Center")      return HomeCorner::Center;
    return HomeCorner::BottomLeft;
}

// --- JSON serialization ---

std::string MachineProfile::toJsonString() const {
    nlohmann::json j{
        {"name", name},
        {"maxFeedRateX", maxFeedRateX},
        {"maxFeedRateY", maxFeedRateY},
        {"maxFeedRateZ", maxFeedRateZ},
        {"accelX", accelX},
        {"accelY", accelY},
        {"accelZ", accelZ},
        {"maxTravelX", maxTravelX},
        {"maxTravelY", maxTravelY},
        {"maxTravelZ", maxTravelZ},
        {"junctionDeviation", junctionDeviation},
        {"rapidRate", rapidRate},
        {"defaultFeedRate", defaultFeedRate},
        {"defaultPlungeRate", defaultPlungeRate},
        {"defaultStepdown", defaultStepdown},
        // Connection
        {"connectionType", connectionTypeToString(connectionType)},
        {"preferredFirmware", firmwareTypeToString(preferredFirmware)},
        {"baudRate", baudRate},
        {"tcpHost", tcpHost},
        {"tcpPort", tcpPort},
        // Spindle
        {"spindleMaxRPM", spindleMaxRPM},
        {"spindlePower", spindlePower},
        {"spindleReverse", spindleReverse},
        // Drive
        {"driveSystem", driveSystemToString(driveSystem)},
        {"customRigidityFactor", normalizeRigidityFactor(customRigidityFactor)},
        // Homing
        {"homeCorner", homeCornerToString(homeCorner)},
        // Auxiliary
        {"hasDustCollection", hasDustCollection},
        {"hasCoolant", hasCoolant},
        {"hasMistCoolant", hasMistCoolant},
        {"hasProbe", hasProbe},
        {"hasToolChanger", hasToolChanger},
        {"hasToolLengthOffset", hasToolLengthOffset},
    };
    return j.dump();
}

MachineProfile MachineProfile::fromJsonString(const std::string& jsonStr) {
    auto j = nlohmann::json::parse(jsonStr, nullptr, false);
    if (j.is_discarded())
        return MachineProfile{};

    MachineProfile p;
    if (j.contains("name")) p.name = j["name"].get<std::string>();
    if (j.contains("maxFeedRateX")) p.maxFeedRateX = j["maxFeedRateX"].get<f32>();
    if (j.contains("maxFeedRateY")) p.maxFeedRateY = j["maxFeedRateY"].get<f32>();
    if (j.contains("maxFeedRateZ")) p.maxFeedRateZ = j["maxFeedRateZ"].get<f32>();
    if (j.contains("accelX")) p.accelX = j["accelX"].get<f32>();
    if (j.contains("accelY")) p.accelY = j["accelY"].get<f32>();
    if (j.contains("accelZ")) p.accelZ = j["accelZ"].get<f32>();
    if (j.contains("maxTravelX")) p.maxTravelX = j["maxTravelX"].get<f32>();
    if (j.contains("maxTravelY")) p.maxTravelY = j["maxTravelY"].get<f32>();
    if (j.contains("maxTravelZ")) p.maxTravelZ = j["maxTravelZ"].get<f32>();
    if (j.contains("junctionDeviation")) p.junctionDeviation = j["junctionDeviation"].get<f32>();
    if (j.contains("rapidRate")) p.rapidRate = j["rapidRate"].get<f32>();
    if (j.contains("defaultFeedRate")) p.defaultFeedRate = j["defaultFeedRate"].get<f32>();
    if (j.contains("defaultPlungeRate")) p.defaultPlungeRate = j["defaultPlungeRate"].get<f32>();
    if (j.contains("defaultStepdown")) p.defaultStepdown = j["defaultStepdown"].get<f32>();
    // Connection
    if (j.contains("connectionType")) p.connectionType = connectionTypeFromString(j["connectionType"].get<std::string>());
    if (j.contains("preferredFirmware")) p.preferredFirmware = firmwareTypeFromString(j["preferredFirmware"].get<std::string>());
    if (j.contains("baudRate")) p.baudRate = j["baudRate"].get<int>();
    if (j.contains("tcpHost")) p.tcpHost = j["tcpHost"].get<std::string>();
    if (j.contains("tcpPort")) p.tcpPort = j["tcpPort"].get<int>();
    // Spindle
    if (j.contains("spindleMaxRPM")) p.spindleMaxRPM = j["spindleMaxRPM"].get<f32>();
    if (j.contains("spindlePower")) p.spindlePower = j["spindlePower"].get<f32>();
    if (j.contains("spindleReverse")) p.spindleReverse = j["spindleReverse"].get<bool>();
    // Drive
    if (j.contains("driveSystem")) p.driveSystem = driveSystemFromString(j["driveSystem"].get<std::string>());
    if (const auto it = j.find("customRigidityFactor"); it != j.end() && it->is_number()) {
        p.customRigidityFactor = static_cast<f32>(normalizeRigidityFactor(it->get<f64>()));
    }
    // Homing
    if (j.contains("homeCorner")) p.homeCorner = homeCornerFromString(j["homeCorner"].get<std::string>());
    // Auxiliary
    if (j.contains("hasDustCollection")) p.hasDustCollection = j["hasDustCollection"].get<bool>();
    if (j.contains("hasCoolant")) p.hasCoolant = j["hasCoolant"].get<bool>();
    if (j.contains("hasMistCoolant")) p.hasMistCoolant = j["hasMistCoolant"].get<bool>();
    if (j.contains("hasProbe")) p.hasProbe = j["hasProbe"].get<bool>();
    if (j.contains("hasToolChanger")) p.hasToolChanger = j["hasToolChanger"].get<bool>();
    if (j.contains("hasToolLengthOffset")) p.hasToolLengthOffset = j["hasToolLengthOffset"].get<bool>();
    return p;
}

// --- Preset factories ---

MachineProfile MachineProfile::defaultProfile() {
    MachineProfile p;
    p.builtIn = true;
    return p;
}

// --- Sienci Labs ---

MachineProfile MachineProfile::longmillMK2() {
    MachineProfile p;
    p.name = "Sienci LongMill MK2 30x30";
    p.builtIn = true;
    p.maxFeedRateX = 4000.0f;  p.maxFeedRateY = 4000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 820.0f;  p.maxTravelY = 868.0f;  p.maxTravelZ = 125.0f;
    p.rapidRate = 4000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::Acme;
    p.spindleMaxRPM = 30000.0f;  // Makita RT0701C typical
    return p;
}

MachineProfile MachineProfile::longmillMK2_48x30() {
    MachineProfile p;
    p.name = "Sienci LongMill MK2 48x30";
    p.builtIn = true;
    p.maxFeedRateX = 4000.0f;  p.maxFeedRateY = 4000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 1278.0f;  p.maxTravelY = 868.0f;  p.maxTravelZ = 125.0f;
    p.rapidRate = 4000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::Acme;
    p.spindleMaxRPM = 30000.0f;
    return p;
}

MachineProfile MachineProfile::altmill48() {
    MachineProfile p;
    p.name = "Sienci AltMill 48x48";
    p.builtIn = true;
    p.maxFeedRateX = 15000.0f;  p.maxFeedRateY = 15000.0f;  p.maxFeedRateZ = 6000.0f;
    p.accelX = 500.0f;  p.accelY = 500.0f;  p.accelZ = 200.0f;
    p.maxTravelX = 1265.0f;  p.maxTravelY = 1251.0f;  p.maxTravelZ = 174.0f;
    p.rapidRate = 15000.0f;  p.defaultFeedRate = 2000.0f;
    p.driveSystem = DriveSystem::BallScrew;
    p.spindleMaxRPM = 24000.0f;
    p.hasProbe = true;
    return p;
}

// --- Shapeoko (Carbide 3D) ---

MachineProfile MachineProfile::shapeoko4() {
    MachineProfile p;
    p.name = "Shapeoko 4 XXL";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 5000.0f;
    p.accelX = 400.0f;  p.accelY = 400.0f;  p.accelZ = 200.0f;
    p.maxTravelX = 838.0f;  p.maxTravelY = 838.0f;  p.maxTravelZ = 102.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::Belt;
    p.spindleMaxRPM = 30000.0f;  // Carbide Compact Router
    p.hasProbe = true;
    return p;
}

MachineProfile MachineProfile::shapeoko5Pro() {
    MachineProfile p;
    p.name = "Shapeoko 5 Pro 4x4";
    p.builtIn = true;
    p.maxFeedRateX = 10000.0f;  p.maxFeedRateY = 10000.0f;  p.maxFeedRateZ = 5000.0f;
    p.accelX = 500.0f;  p.accelY = 500.0f;  p.accelZ = 200.0f;
    p.maxTravelX = 1237.0f;  p.maxTravelY = 1237.0f;  p.maxTravelZ = 155.0f;
    p.rapidRate = 10000.0f;  p.defaultFeedRate = 1500.0f;
    p.driveSystem = DriveSystem::Belt;
    p.spindleMaxRPM = 30000.0f;
    p.hasProbe = true;
    return p;
}

// --- OneFinity ---

MachineProfile MachineProfile::onefinityWoodworker() {
    MachineProfile p;
    p.name = "OneFinity Woodworker";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 5000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 819.0f;  p.maxTravelY = 819.0f;  p.maxTravelZ = 133.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::BallScrew;
    p.spindleMaxRPM = 30000.0f;
    return p;
}

MachineProfile MachineProfile::onefinityJourneyman() {
    MachineProfile p;
    p.name = "OneFinity Journeyman";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 5000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 1220.0f;  p.maxTravelY = 819.0f;  p.maxTravelZ = 133.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::BallScrew;
    p.spindleMaxRPM = 30000.0f;
    return p;
}

MachineProfile MachineProfile::onefinityForeman() {
    MachineProfile p;
    p.name = "OneFinity Foreman";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 5000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 1220.0f;  p.maxTravelY = 1220.0f;  p.maxTravelZ = 133.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::BallScrew;
    p.spindleMaxRPM = 30000.0f;
    return p;
}

// --- FoxAlien ---

MachineProfile MachineProfile::foxalienMasuter() {
    MachineProfile p;
    p.name = "FoxAlien Masuter Pro";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 400.0f;  p.maxTravelY = 400.0f;  p.maxTravelZ = 60.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 800.0f;
    p.connectionType = ConnectionType::Serial;
    p.driveSystem = DriveSystem::LeadScrew;
    p.spindleMaxRPM = 10000.0f;
    return p;
}

MachineProfile MachineProfile::foxalienVasto() {
    MachineProfile p;
    p.name = "FoxAlien Vasto";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 400.0f;  p.maxTravelY = 400.0f;  p.maxTravelZ = 95.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.connectionType = ConnectionType::Serial;
    p.driveSystem = DriveSystem::LeadScrew;
    p.spindleMaxRPM = 10000.0f;
    return p;
}

MachineProfile MachineProfile::foxalien8040() {
    MachineProfile p;
    p.name = "FoxAlien 8040";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 800.0f;  p.maxTravelY = 400.0f;  p.maxTravelZ = 95.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.connectionType = ConnectionType::Serial;
    p.driveSystem = DriveSystem::BallScrew;
    p.spindleMaxRPM = 10000.0f;
    p.spindlePower = 400.0f;
    return p;
}

// --- Genmitsu (SainSmart) ---

MachineProfile MachineProfile::genmitsu3018() {
    MachineProfile p;
    p.name = "Genmitsu 3018-PROVer";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 300.0f;  p.maxTravelY = 180.0f;  p.maxTravelZ = 45.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 500.0f;
    p.driveSystem = DriveSystem::LeadScrew;
    p.spindleMaxRPM = 10000.0f;
    return p;
}

MachineProfile MachineProfile::genmitsu4030() {
    MachineProfile p;
    p.name = "Genmitsu PROVerXL 4030";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 400.0f;  p.maxTravelY = 300.0f;  p.maxTravelZ = 110.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 800.0f;
    p.driveSystem = DriveSystem::LeadScrew;
    p.spindleMaxRPM = 10000.0f;
    return p;
}

// --- X-Carve (Inventables) ---

MachineProfile MachineProfile::xcarve() {
    MachineProfile p;
    p.name = "X-Carve 1000mm";
    p.builtIn = true;
    p.maxFeedRateX = 8000.0f;  p.maxFeedRateY = 8000.0f;  p.maxFeedRateZ = 5000.0f;
    p.accelX = 250.0f;  p.accelY = 250.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 750.0f;  p.maxTravelY = 750.0f;  p.maxTravelZ = 114.0f;
    p.rapidRate = 8000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::Belt;
    p.spindleMaxRPM = 30000.0f;  // DeWalt 611
    return p;
}

// --- BobsCNC ---

MachineProfile MachineProfile::bobscncE4() {
    MachineProfile p;
    p.name = "BobsCNC Evolution 4";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 610.0f;  p.maxTravelY = 610.0f;  p.maxTravelZ = 85.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 800.0f;
    p.driveSystem = DriveSystem::Belt;
    p.spindleMaxRPM = 30000.0f;  // Makita RT0701C typical
    return p;
}

// --- OpenBuilds ---

MachineProfile MachineProfile::openbuildsLead1010() {
    MachineProfile p;
    p.name = "OpenBuilds LEAD 1010";
    p.builtIn = true;
    p.maxFeedRateX = 7500.0f;  p.maxFeedRateY = 7500.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 250.0f;  p.accelY = 250.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 737.0f;  p.maxTravelY = 813.0f;  p.maxTravelZ = 102.0f;
    p.rapidRate = 7500.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::LeadScrew;
    p.spindleMaxRPM = 30000.0f;
    return p;
}

MachineProfile MachineProfile::openbuildsLead1515() {
    MachineProfile p;
    p.name = "OpenBuilds LEAD 1515";
    p.builtIn = true;
    p.maxFeedRateX = 7500.0f;  p.maxFeedRateY = 7500.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 250.0f;  p.accelY = 250.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 1168.0f;  p.maxTravelY = 1245.0f;  p.maxTravelZ = 89.0f;
    p.rapidRate = 7500.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::LeadScrew;
    p.spindleMaxRPM = 30000.0f;
    return p;
}

// --- MillRight ---

MachineProfile MachineProfile::millrightMegaV() {
    MachineProfile p;
    p.name = "MillRight Mega V XL";
    p.builtIn = true;
    p.maxFeedRateX = 5000.0f;  p.maxFeedRateY = 5000.0f;  p.maxFeedRateZ = 3000.0f;
    p.accelX = 200.0f;  p.accelY = 200.0f;  p.accelZ = 100.0f;
    p.maxTravelX = 889.0f;  p.maxTravelY = 889.0f;  p.maxTravelZ = 159.0f;
    p.rapidRate = 5000.0f;  p.defaultFeedRate = 1000.0f;
    p.driveSystem = DriveSystem::Belt;
    return p;
}

// --- All built-in presets ---

std::vector<MachineProfile> MachineProfile::allBuiltInPresets() {
    return {
        defaultProfile(),
        // Sienci Labs
        longmillMK2(),
        longmillMK2_48x30(),
        altmill48(),
        // Shapeoko
        shapeoko4(),
        shapeoko5Pro(),
        // OneFinity
        onefinityWoodworker(),
        onefinityJourneyman(),
        onefinityForeman(),
        // FoxAlien
        foxalienMasuter(),
        foxalienVasto(),
        foxalien8040(),
        // Genmitsu
        genmitsu3018(),
        genmitsu4030(),
        // X-Carve
        xcarve(),
        // BobsCNC
        bobscncE4(),
        // OpenBuilds
        openbuildsLead1010(),
        openbuildsLead1515(),
        // MillRight
        millrightMegaV(),
    };
}

} // namespace gcode
} // namespace dw
