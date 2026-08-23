#pragma once

#include <vector>

#include "machine_profile.h"

namespace dw::gcode {

// Produces a persistable, non-built-in profile from an edited preset. The
// returned name is unique among the profiles already loaded by Config.
MachineProfile makeEditableMachineProfileCopy(
    const MachineProfile& source,
    const MachineProfile& edited,
    const std::vector<MachineProfile>& existingProfiles);

} // namespace dw::gcode
