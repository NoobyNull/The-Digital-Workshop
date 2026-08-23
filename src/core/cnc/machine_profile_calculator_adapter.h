#pragma once

#include "../gcode/machine_profile.h"
#include "tool_calculator.h"

namespace dw {

// Compatibility mapping for code that still consumes the legacy VTDB drive enum.
DriveType calculatorDriveType(gcode::DriveSystem driveSystem) noexcept;

// Applies only machine-owned calculation inputs. Tool and material inputs are
// intentionally left untouched.
void applyMachineProfileToCalcInput(
    const gcode::MachineProfile& profile,
    CalcInput& input) noexcept;

} // namespace dw
