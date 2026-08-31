#pragma once

#include <optional>

#include "../types.h"
#include "cnc_tool.h"

namespace dw {

// Material hardness band for chip load / SFM lookup
enum class HardnessBand {
    Soft,      // Janka < 800 (pine, cedar, basswood, foam)
    Medium,    // Janka 800-1500 (cherry, walnut, oak, maple)
    Hard,      // Janka 1500-2500 (hickory, ipe)
    VeryHard,  // Janka > 2500 (ebony, lignum vitae)
    Composite, // MDF, plywood, particle board (Janka == 0)
    Metal,     // Aluminum, brass
    Plastic    // HDPE, acrylic
};

// Input parameters for tool calculation
struct CalcInput {
    // Tool geometry
    f64 diameter = 0.0;      // inches (or mm if metric)
    int num_flutes = 2;
    VtdbToolType tool_type = VtdbToolType::EndMill;
    VtdbUnits units = VtdbUnits::Imperial;

    // Material
    f64 janka_hardness = 0.0; // lbf (0 for composites/metals/plastics)
    std::string material_name; // for composite/metal/plastic classification

    // Machine
    f64 spindle_power_watts = 0.0;
    int max_rpm = 24000;
    DriveType drive_type = DriveType::Belt;
    std::optional<f64> rigidity_factor_override;
};

// Output: recommended cutting parameters
struct CalcResult {
    int rpm = 0;
    f64 feed_rate = 0.0;      // in/min or mm/min
    f64 plunge_rate = 0.0;    // in/min or mm/min
    f64 stepdown = 0.0;       // in or mm (depth of cut)
    f64 stepover = 0.0;       // in or mm
    f64 chip_load = 0.0;      // in or mm per tooth
    f64 power_required = 0.0; // watts
    bool power_limited = false;
    HardnessBand hardness_band = HardnessBand::Medium;
    f64 rigidity_factor = 1.0;
};

// Pure calculation engine - no DB dependencies, fully unit-testable
class ToolCalculator {
  public:
    // Classify material into hardness band
    static HardnessBand classifyMaterial(f64 jankaHardness, const std::string& materialName);

    // Get rigidity derating factor for drive type
    static f64 rigidityFactor(DriveType driveType);

    // Calculate recommended surface feet per minute
    static f64 recommendedSFM(HardnessBand band, VtdbToolType toolType);

    // Look up conservative chip load (inches per tooth)
    static f64 chipLoad(HardnessBand band, f64 diameterInches, int flutes);

    // Calculate RPM from SFM and diameter, clamped to max
    static int calculateRPM(f64 sfm, f64 diameterInches, int maxRPM);

    // Estimate specific cutting energy (watts per cubic inch/min) from hardness band
    static f64 specificCuttingEnergy(HardnessBand band);

    // Full calculation: takes all inputs, returns recommended parameters
    static CalcResult calculate(const CalcInput& input);
};

} // namespace dw
