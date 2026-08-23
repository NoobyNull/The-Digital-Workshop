#pragma once

#include <string>

#include "../cnc/cnc_tool.h"

namespace dw::carve {

// Keeps Generic XY-probe compensation attached to the tool that runs first.
// Automatic values follow tool-plan changes; an operator-entered value remains
// stable until they explicitly return to automatic tracking.
class DirectCarveProbeToolDiameter {
  public:
    [[nodiscard]] f32 valueMm() const noexcept;
    [[nodiscard]] bool manualOverride() const noexcept;

    // Returns true when the effective value changed.
    [[nodiscard]] bool refreshAutomatic(const VtdbToolGeometry* initialTool);
    void setManualValue(f32 diameterMm) noexcept;
    [[nodiscard]] bool resumeAutomatic(const VtdbToolGeometry* initialTool);
    void reset() noexcept;

  private:
    f32 m_valueMm = 0.0F;
    bool m_manualOverride = false;
    std::string m_automaticSourceIdentity;
};

} // namespace dw::carve
