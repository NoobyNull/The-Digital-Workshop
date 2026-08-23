#include "tool_calculator.h"

#include "../gcode/machine_rigidity.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dw {

namespace {

bool isRouterMaterial(HardnessBand band) {
    switch (band) {
    case HardnessBand::Soft:
    case HardnessBand::Medium:
    case HardnessBand::Hard:
    case HardnessBand::VeryHard:
    case HardnessBand::Composite:
    case HardnessBand::Plastic:
        return true;
    case HardnessBand::Metal:
        return false;
    }
    return false;
}

bool isRouterCuttingTool(VtdbToolType toolType) {
    switch (toolType) {
    case VtdbToolType::BallNose:
    case VtdbToolType::EndMill:
    case VtdbToolType::Radiused:
    case VtdbToolType::VBit:
    case VtdbToolType::TaperedBallNose:
        return true;
    default:
        return false;
    }
}

} // namespace

HardnessBand ToolCalculator::classifyMaterial(f64 jankaHardness, const std::string& name) {
    // Zero Janka means non-wood — classify by name
    if (jankaHardness <= 0.0) {
        // Metal detection
        if (name.find("Aluminum") != std::string::npos ||
            name.find("Brass") != std::string::npos ||
            name.find("Steel") != std::string::npos ||
            name.find("Copper") != std::string::npos) {
            return HardnessBand::Metal;
        }
        // Plastic detection
        if (name.find("HDPE") != std::string::npos ||
            name.find("Acrylic") != std::string::npos ||
            name.find("PVC") != std::string::npos ||
            name.find("Nylon") != std::string::npos ||
            name.find("Delrin") != std::string::npos ||
            name.find("UHMW") != std::string::npos ||
            name.find("Foam") != std::string::npos) {
            return HardnessBand::Plastic;
        }
        // Default non-wood: composite (MDF, plywood, etc.)
        return HardnessBand::Composite;
    }

    if (jankaHardness < 800.0) return HardnessBand::Soft;
    if (jankaHardness < 1500.0) return HardnessBand::Medium;
    if (jankaHardness < 2500.0) return HardnessBand::Hard;
    return HardnessBand::VeryHard;
}

f64 ToolCalculator::rigidityFactor(DriveType driveType) {
    switch (driveType) {
    case DriveType::Belt: return 0.80;
    case DriveType::LeadScrew: return 0.90;
    case DriveType::BallScrew:
    case DriveType::RackPinion: return 1.00;
    }
    return 0.80; // safe default
}

// Conservative SFM values for beginner-safe operation
// These are the LOW end of published ranges
f64 ToolCalculator::recommendedSFM(HardnessBand band, VtdbToolType toolType) {
    // Base SFM by material hardness band (for carbide end mills)
    f64 baseSFM = 0.0;
    switch (band) {
    case HardnessBand::Soft: baseSFM = 600.0; break;      // Pine, cedar
    case HardnessBand::Medium: baseSFM = 500.0; break;    // Oak, cherry, walnut
    case HardnessBand::Hard: baseSFM = 400.0; break;      // Hickory, hard maple
    case HardnessBand::VeryHard: baseSFM = 300.0; break;  // Ipe, ebony
    case HardnessBand::Composite: baseSFM = 500.0; break; // MDF, plywood
    case HardnessBand::Metal: baseSFM = 200.0; break;     // Aluminum
    case HardnessBand::Plastic: baseSFM = 400.0; break;   // HDPE, acrylic
    }

    // Adjust by tool type
    switch (toolType) {
    case VtdbToolType::BallNose: return baseSFM * 0.85; // Effective diameter is smaller
    case VtdbToolType::VBit: return baseSFM * 0.70;     // Small effective cutting area
    case VtdbToolType::Drill: return baseSFM * 0.60;    // Plunging only
    default: return baseSFM;
    }
}

// Conservative chip load table (inches per tooth)
// Indexed by hardness band and diameter range
f64 ToolCalculator::chipLoad(HardnessBand band, f64 diameterInches, int flutes) {
    if (diameterInches <= 0.0 || flutes <= 0) return 0.0;

    // Base chip load by diameter (for 2-flute end mill in medium wood)
    // These are conservative — about 60-70% of manufacturer recommended
    f64 baseChipLoad = 0.0;
    if (diameterInches <= 0.0625) {       // 1/16"
        baseChipLoad = 0.002;
    } else if (diameterInches <= 0.125) { // 1/8"
        baseChipLoad = 0.003;
    } else if (diameterInches <= 0.250) { // 1/4"
        baseChipLoad = 0.005;
    } else if (diameterInches <= 0.375) { // 3/8"
        baseChipLoad = 0.006;
    } else if (diameterInches <= 0.500) { // 1/2"
        baseChipLoad = 0.007;
    } else {                              // > 1/2"
        baseChipLoad = 0.008;
    }

    // Adjust for material hardness
    f64 hardnessFactor = 1.0;
    switch (band) {
    case HardnessBand::Soft: hardnessFactor = 1.3; break;      // Can be more aggressive
    case HardnessBand::Medium: hardnessFactor = 1.0; break;    // Baseline
    case HardnessBand::Hard: hardnessFactor = 0.75; break;     // More conservative
    case HardnessBand::VeryHard: hardnessFactor = 0.55; break; // Very conservative
    case HardnessBand::Composite: hardnessFactor = 1.1; break; // Similar to soft wood
    case HardnessBand::Metal: hardnessFactor = 0.35; break;    // Much lower for metal
    case HardnessBand::Plastic: hardnessFactor = 1.2; break;   // Plastics are forgiving
    }

    // More flutes = reduce chip load per tooth (but increases overall feed)
    // 2-flute is baseline; 3+ flute needs lower per-tooth load
    f64 fluteFactor = (flutes <= 2) ? 1.0 : (2.0 / flutes);

    return baseChipLoad * hardnessFactor * fluteFactor;
}

int ToolCalculator::calculateRPM(f64 sfm, f64 diameterInches, int maxRPM) {
    if (diameterInches <= 0.0) return 0;

    // RPM = (SFM * 12) / (pi * diameter)
    int rpm = static_cast<int>((sfm * 12.0) / (M_PI * diameterInches));
    return std::min(rpm, maxRPM);
}

// Specific cutting energy in watts per (cubic inch per minute)
// This is used for power estimation
f64 ToolCalculator::specificCuttingEnergy(HardnessBand band) {
    switch (band) {
    case HardnessBand::Soft: return 5.0;
    case HardnessBand::Medium: return 8.0;
    case HardnessBand::Hard: return 12.0;
    case HardnessBand::VeryHard: return 18.0;
    case HardnessBand::Composite: return 6.0;
    case HardnessBand::Metal: return 50.0;
    case HardnessBand::Plastic: return 3.0;
    }
    return 8.0;
}

CalcResult ToolCalculator::calculate(const CalcInput& input) {
    CalcResult result;

    if (input.diameter <= 0.0 || input.num_flutes <= 0) return result;

    // Convert to inches if metric
    f64 diameterInches = input.diameter;
    bool isMetric = (input.units == VtdbUnits::Metric);
    if (isMetric) {
        diameterInches = input.diameter / 25.4;
    }

    // 1. Classify material
    result.hardness_band = classifyMaterial(input.janka_hardness, input.material_name);

    // 2. Get rigidity derating
    result.rigidity_factor = input.rigidity_factor_override.has_value()
        ? gcode::normalizeRigidityFactor(*input.rigidity_factor_override)
        : rigidityFactor(input.drive_type);

    if (input.max_rpm <= 0) return result;

    // 3. Select spindle RPM. Router woodworking and plastic jobs normally use
    // the configured spindle RPM, then feed is derived from chip load. Keep
    // SFM-limited RPM for metals and non-cutting/specialty tool types.
    f64 sfm = recommendedSFM(result.hardness_band, input.tool_type);
    if (isRouterMaterial(result.hardness_band) && isRouterCuttingTool(input.tool_type)) {
        result.rpm = input.max_rpm;
    } else {
        result.rpm = calculateRPM(sfm, diameterInches, input.max_rpm);
    }
    if (result.rpm <= 0) return result;

    // 4. Get chip load
    result.chip_load = chipLoad(result.hardness_band, diameterInches, input.num_flutes);

    // 5. Feed rate = RPM * flutes * chip_load * rigidity_factor
    f64 feedRate = result.rpm * input.num_flutes * result.chip_load * result.rigidity_factor;

    // 6. Plunge rate = 50% of feed rate (30% for metals)
    f64 plungeFactor = (result.hardness_band == HardnessBand::Metal) ? 0.30 : 0.50;
    f64 plungeRate = feedRate * plungeFactor;

    // 7. Depth of cut = fraction of diameter based on rigidity
    f64 docFactor = 0.0;
    switch (result.hardness_band) {
    case HardnessBand::Soft: docFactor = 1.0; break;
    case HardnessBand::Medium: docFactor = 0.75; break;
    case HardnessBand::Hard: docFactor = 0.50; break;
    case HardnessBand::VeryHard: docFactor = 0.30; break;
    case HardnessBand::Composite: docFactor = 0.75; break;
    case HardnessBand::Metal: docFactor = 0.20; break;
    case HardnessBand::Plastic: docFactor = 1.0; break;
    }
    f64 stepdown = diameterInches * docFactor * result.rigidity_factor;

    // 8. Stepover = 40% of diameter (conservative roughing default)
    f64 stepover = diameterInches * 0.40;

    // 9. Power check
    f64 mrr = stepover * stepdown * feedRate; // cubic inches per minute
    f64 sce = specificCuttingEnergy(result.hardness_band);
    result.power_required = mrr * sce;

    // If spindle power specified, reduce DOC until power fits
    if (input.spindle_power_watts > 0.0 && result.power_required > input.spindle_power_watts) {
        result.power_limited = true;
        // Scale down DOC proportionally
        f64 powerRatio = input.spindle_power_watts / result.power_required;
        stepdown *= powerRatio;
        // Recalculate MRR and power
        mrr = stepover * stepdown * feedRate;
        result.power_required = mrr * sce;
    }

    // Convert back to metric if needed
    if (isMetric) {
        result.feed_rate = feedRate * 25.4;
        result.plunge_rate = plungeRate * 25.4;
        result.stepdown = stepdown * 25.4;
        result.stepover = stepover * 25.4;
        result.chip_load = result.chip_load * 25.4;
    } else {
        result.feed_rate = feedRate;
        result.plunge_rate = plungeRate;
        result.stepdown = stepdown;
        result.stepover = stepover;
    }

    return result;
}

} // namespace dw
