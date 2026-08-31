#include "experience_router.h"

namespace dw::workshop {

ExperienceRouter::ExperienceRouter(WorkshopCommandTarget& target,
                                   bool guidedEnabled) noexcept
    : m_target(target), m_guidedEnabled(guidedEnabled) {}

void ExperienceRouter::setGuidedEnabled(bool enabled) noexcept {
    m_guidedEnabled = enabled;
}

bool ExperienceRouter::guidedEnabled() const noexcept {
    return m_guidedEnabled;
}

WorkshopTransition ExperienceRouter::dispatch(ExperienceMode source,
                                              const WorkshopCommand& command) {
    if (source == ExperienceMode::Guided && !m_guidedEnabled) {
        return {
            TransitionStatus::Rejected,
            TransitionReason::ExperienceDisabled,
            m_target.snapshot(),
        };
    }

    return m_target.dispatch(command);
}

} // namespace dw::workshop
