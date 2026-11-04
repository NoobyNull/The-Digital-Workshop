#include "window_mode_policy.h"

namespace dw {

bool windowAvailableInMode(WindowRole role, WorkspaceMode mode) {
    switch (role) {
    case WindowRole::Workshop:
        return mode == WorkspaceMode::Model;
    case WindowRole::Sender:
        return mode == WorkspaceMode::CNC;
    case WindowRole::Shared:
    case WindowRole::Global:
    case WindowRole::Shell:
    case WindowRole::Local:
        return true;
    }
    return true;
}

WindowOpenAction windowOpenAction(WindowRole role,
                                  WorkspaceMode currentMode,
                                  const WindowOpenContext& context) {
    // The modal Library flow owns the screen; machine surfaces stay out.
    if (role == WindowRole::Sender && context.libraryOverlayActive)
        return WindowOpenAction::Blocked;

    if (windowAvailableInMode(role, currentMode))
        return WindowOpenAction::Open;

    // Cross-mode request: a running stream pins the workspace where it is.
    if (context.cncStreaming)
        return WindowOpenAction::Blocked;
    return role == WindowRole::Sender ? WindowOpenAction::SwitchToCnc
                                      : WindowOpenAction::SwitchToModel;
}

} // namespace dw
