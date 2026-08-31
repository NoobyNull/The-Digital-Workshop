#include "machine_profile_customization.h"

#include <algorithm>

namespace dw::gcode {
namespace {

bool profileNameExists(
    const std::string& name,
    const std::vector<MachineProfile>& profiles)
{
    return std::any_of(
        profiles.begin(),
        profiles.end(),
        [&name](const MachineProfile& profile) { return profile.name == name; });
}

std::string uniqueCustomName(
    const std::string& preferredName,
    const std::vector<MachineProfile>& profiles)
{
    if (!profileNameExists(preferredName, profiles)) {
        return preferredName;
    }

    for (int copyNumber = 2;; ++copyNumber) {
        const std::string numberedName =
            preferredName + " " + std::to_string(copyNumber);
        if (!profileNameExists(numberedName, profiles)) {
            return numberedName;
        }
    }
}

} // namespace

MachineProfile makeEditableMachineProfileCopy(
    const MachineProfile& source,
    const MachineProfile& edited,
    const std::vector<MachineProfile>& existingProfiles)
{
    MachineProfile copy = edited;
    copy.builtIn = false;

    std::string preferredName = edited.name;
    if (preferredName.empty() || preferredName == source.name) {
        preferredName = source.name + " (Custom)";
    }
    copy.name = uniqueCustomName(preferredName, existingProfiles);
    return copy;
}

} // namespace dw::gcode
