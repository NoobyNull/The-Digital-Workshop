#pragma once

#include "../types.h"

namespace dw {
namespace gcode {

enum class DriveSystem;
struct MachineProfile;

inline constexpr f64 kMinimumRigidityFactor = 0.10;
inline constexpr f64 kMaximumRigidityFactor = 1.00;
inline constexpr f64 kSafeRigidityFactor = 0.80;

// Human-readable name for machine setup and recommendation summaries.
const char* driveSystemDisplayName(DriveSystem driveSystem) noexcept;

// Keeps user-provided values inside the conservative recommendation range.
// Non-finite values fall back to the safe belt-equivalent factor.
f64 normalizeRigidityFactor(f64 factor) noexcept;

// Built-in recommendation derating for each drive family.
f64 defaultRigidityFactor(DriveSystem driveSystem) noexcept;

// Custom profiles use their stored factor; known drive families use defaults.
f64 effectiveRigidityFactor(const MachineProfile& profile) noexcept;

} // namespace gcode
} // namespace dw
