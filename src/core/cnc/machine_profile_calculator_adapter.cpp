#include "machine_profile_calculator_adapter.h"

#include "../gcode/machine_rigidity.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dw {

DriveType calculatorDriveType(gcode::DriveSystem driveSystem) noexcept {
    switch (driveSystem) {
    case gcode::DriveSystem::Belt: return DriveType::Belt;
    case gcode::DriveSystem::Acme:
    case gcode::DriveSystem::LeadScrew: return DriveType::LeadScrew;
    case gcode::DriveSystem::BallScrew: return DriveType::BallScrew;
    case gcode::DriveSystem::Custom: return DriveType::Belt;
    }
    return DriveType::Belt;
}

void applyMachineProfileToCalcInput(
    const gcode::MachineProfile& profile,
    CalcInput& input) noexcept {
    input.spindle_power_watts = std::isfinite(profile.spindlePower)
        ? std::max(0.0, static_cast<f64>(profile.spindlePower))
        : 0.0;

    if (std::isfinite(profile.spindleMaxRPM) && profile.spindleMaxRPM > 0.0f) {
        const auto maximum = static_cast<f64>(std::numeric_limits<int>::max());
        input.max_rpm = static_cast<int>(
            std::min(static_cast<f64>(profile.spindleMaxRPM), maximum));
    } else {
        input.max_rpm = 0;
    }

    input.drive_type = calculatorDriveType(profile.driveSystem);
    input.rigidity_factor_override = gcode::effectiveRigidityFactor(profile);
}

} // namespace dw
