#pragma once

#include <optional>

#include "modules/workshop/experience_router.h"

namespace dw::workshop {

class ProjectSession final : public WorkshopCommandTarget {
  public:
    ProjectSession() = default;

    [[nodiscard]] WorkshopContextSnapshot snapshot() const override;
    WorkshopTransition dispatch(const WorkshopCommand& command) override;

    [[nodiscard]] bool hasPendingTransition() const noexcept;

  private:
    WorkshopTransition dispatchInternal(const WorkshopCommand& command);

    WorkshopTransition handle(const WorkshopCommand& command,
                              const ActivateProject& activate);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const CloseProject& close);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const SelectProjectItem& select);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const ClearProjectItem& clear);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const NavigateTo& navigate);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const PreviewLibraryItem& preview);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const ReturnFromLibrary& returnFromLibrary);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const SetProjectDirty& setDirty);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const SetPreparationLock& setLock);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const BeginRun& beginRun);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const EndRun& endRun);
    WorkshopTransition handle(const WorkshopCommand& command,
                              const ResolvePendingTransition& resolve);

    WorkshopTransition applied();
    [[nodiscard]] WorkshopTransition unchanged() const;
    [[nodiscard]] WorkshopTransition rejected(TransitionStatus status,
                                              TransitionReason reason) const;
    WorkshopTransition requestConfirmation(const WorkshopCommand& command,
                                            PendingChanges pendingChanges);

    [[nodiscard]] PendingChanges unresolvedProjectChange() const noexcept;
    void clearPreviewAndReturn();
    void restoreProjectOrigin();

    WorkshopContextSnapshot m_context;
    std::optional<WorkshopCommand> m_pendingCommand;
    std::optional<ConfirmationToken> m_pendingConfirmation;
    PendingChanges m_pendingChanges;
    std::uint64_t m_nextConfirmationValue = 1;
};

} // namespace dw::workshop
