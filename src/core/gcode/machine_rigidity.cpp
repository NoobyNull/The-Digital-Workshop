#include "machine_rigidity.h"

#include "machine_profile.h"

#include <algorithm>
#include <cmath>

namespace dw {
namespace gcode {

const char* driveSystemDisplayName(DriveSystem driveSystem) noexcept {
    switch (driveSystem) {
    case DriveSystem::Belt: return "Belt";
    case DriveSystem::Acme: return "Acme";
    case DriveSystem::LeadScrew: return "Lead Screw";
    case DriveSystem::BallScrew: return "Ball Screw";
    case DriveSystem::Custom: return "Custom";
    }
    return "Custom";
}

f64 normalizeRigidityFactor(f64 factor) noexcept {
    if (!std::isfinite(factor)) return kSafeRigidityFactor;
    return std::clamp(factor, kMinimumRigidityFactor, kMaximumRigidityFactor);
}

f64 defaultRigidityFactor(DriveSystem driveSystem) noexcept {
    switch (driveSystem) {
    case DriveSystem::Belt: return 0.80;
    case DriveSystem::Acme:
    case DriveSystem::LeadScrew: return 0.90;
    case DriveSystem::BallScrew: return 1.00;
    case DriveSystem::Custom: return kSafeRigidityFactor;
    }
    return kSafeRigidityFactor;
}

f64 effectiveRigidityFactor(const MachineProfile& profile) noexcept {
    if (profile.driveSystem == DriveSystem::Custom) {
        return normalizeRigidityFactor(profile.customRigidityFactor);
    }
    return defaultRigidityFactor(profile.driveSystem);
}

} // namespace gcode
} // namespace dw
