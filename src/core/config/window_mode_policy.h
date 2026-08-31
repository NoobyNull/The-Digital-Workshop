#pragma once

#include "window_catalog.h"

namespace dw {

// Workspace mode — controls which windows are part of the active surface.
enum class WorkspaceMode { Model, CNC };

// What an open-window request must do given the current mode and context.
enum class WindowOpenAction {
    Open,          // available in the current mode, open in place
    SwitchToModel, // switch workspace to Model, then open
    SwitchToCnc,   // switch workspace to CNC, then open
    Blocked,       // not allowed right now (streaming pin or modal overlay)
};

// Cross-cutting state that can veto or redirect an open request.
struct WindowOpenContext {
    bool cncStreaming = false;         // a program is streaming to the machine
    bool libraryOverlayActive = false; // the modal Library flow is on screen
};

// Rule: is a window of this role part of the given mode's surface?
// Workshop windows belong to Model, Sender windows to CNC; every other role
// (Shared/Global/Shell/Local) spans both modes.
[[nodiscard]] bool windowAvailableInMode(WindowRole role, WorkspaceMode mode);

// Rule: resolve an open request. Cross-mode requests switch the workspace,
// except that an active stream pins the workspace and the Library overlay
// blocks Sender windows outright.
[[nodiscard]] WindowOpenAction windowOpenAction(WindowRole role,
                                                WorkspaceMode currentMode,
                                                const WindowOpenContext& context);

} // namespace dw
