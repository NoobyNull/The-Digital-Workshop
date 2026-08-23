#include "gcode_panel.h"

#include "core/cnc/cnc_controller.h"

namespace dw {

std::optional<GCodePanelRunSnapshot>
GCodePanel::projectPlanRunSnapshot() const noexcept {
    if (m_activeJobId <= 0 || !m_cnc || !m_cnc->isStreaming())
        return std::nullopt;
    return GCodePanelRunSnapshot{
        m_activeJobId,
        m_cnc->isHeld() ? GCodePanelRunState::Paused
                        : GCodePanelRunState::Running,
    };
}

} // namespace dw
