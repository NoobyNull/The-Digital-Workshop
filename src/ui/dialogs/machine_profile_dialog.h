#pragma once

#include <functional>
#include <string>

#include "../../core/gcode/machine_profile.h"

namespace dw {

// Floating window for editing CNC machine profiles
class MachineProfileDialog {
  public:
    MachineProfileDialog() = default;

    void open();
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

    // Called when the active profile changes or is modified (so caller can reanalyze)
    void setOnProfileChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }

    void render();

  private:
    bool m_open = false;
    gcode::MachineProfile m_editProfile;
    int m_editProfileIndex = 0;
    std::string m_saveFeedback;
    std::function<void()> m_onChanged;
};

} // namespace dw
