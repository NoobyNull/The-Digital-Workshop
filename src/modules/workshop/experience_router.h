#pragma once

#include "workshop_contract.h"

namespace dw::workshop {

class WorkshopCommandTarget {
  public:
    virtual ~WorkshopCommandTarget() = default;

    [[nodiscard]] virtual WorkshopContextSnapshot snapshot() const = 0;
    virtual WorkshopTransition dispatch(const WorkshopCommand& command) = 0;
};

class ExperienceRouter {
  public:
    ExperienceRouter(WorkshopCommandTarget& target, bool guidedEnabled) noexcept;

    void setGuidedEnabled(bool enabled) noexcept;
    [[nodiscard]] bool guidedEnabled() const noexcept;

    WorkshopTransition dispatch(ExperienceMode source,
                                const WorkshopCommand& command);

  private:
    WorkshopCommandTarget& m_target;
    bool m_guidedEnabled = false;
};

} // namespace dw::workshop
