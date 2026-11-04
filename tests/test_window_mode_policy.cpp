#include "core/config/window_mode_policy.h"

#include <gtest/gtest.h>

namespace dw {

TEST(WindowModePolicy, RoleAvailabilityMatrix) {
    // Workshop windows belong to Model, Sender windows to CNC.
    EXPECT_TRUE(windowAvailableInMode(WindowRole::Workshop, WorkspaceMode::Model));
    EXPECT_FALSE(windowAvailableInMode(WindowRole::Workshop, WorkspaceMode::CNC));
    EXPECT_FALSE(windowAvailableInMode(WindowRole::Sender, WorkspaceMode::Model));
    EXPECT_TRUE(windowAvailableInMode(WindowRole::Sender, WorkspaceMode::CNC));

    // Every other role spans both modes.
    for (auto role : {WindowRole::Shared, WindowRole::Global, WindowRole::Shell,
                      WindowRole::Local}) {
        EXPECT_TRUE(windowAvailableInMode(role, WorkspaceMode::Model));
        EXPECT_TRUE(windowAvailableInMode(role, WorkspaceMode::CNC));
    }
}

TEST(WindowModePolicy, SameModeRequestsOpenInPlace) {
    WindowOpenContext idle;
    EXPECT_EQ(windowOpenAction(WindowRole::Workshop, WorkspaceMode::Model, idle),
              WindowOpenAction::Open);
    EXPECT_EQ(windowOpenAction(WindowRole::Sender, WorkspaceMode::CNC, idle),
              WindowOpenAction::Open);
    EXPECT_EQ(windowOpenAction(WindowRole::Shared, WorkspaceMode::CNC, idle),
              WindowOpenAction::Open);
}

TEST(WindowModePolicy, CrossModeRequestsSwitchTheWorkspace) {
    WindowOpenContext idle;
    EXPECT_EQ(windowOpenAction(WindowRole::Sender, WorkspaceMode::Model, idle),
              WindowOpenAction::SwitchToCnc);
    EXPECT_EQ(windowOpenAction(WindowRole::Workshop, WorkspaceMode::CNC, idle),
              WindowOpenAction::SwitchToModel);
}

TEST(WindowModePolicy, ActiveStreamPinsTheWorkspace) {
    WindowOpenContext streaming;
    streaming.cncStreaming = true;
    EXPECT_EQ(windowOpenAction(WindowRole::Sender, WorkspaceMode::Model, streaming),
              WindowOpenAction::Blocked);
    EXPECT_EQ(windowOpenAction(WindowRole::Workshop, WorkspaceMode::CNC, streaming),
              WindowOpenAction::Blocked);
    // Same-mode opens stay allowed while streaming.
    EXPECT_EQ(windowOpenAction(WindowRole::Sender, WorkspaceMode::CNC, streaming),
              WindowOpenAction::Open);
}

TEST(WindowModePolicy, LibraryOverlayBlocksSenderWindowsEverywhere) {
    WindowOpenContext library;
    library.libraryOverlayActive = true;
    EXPECT_EQ(windowOpenAction(WindowRole::Sender, WorkspaceMode::Model, library),
              WindowOpenAction::Blocked);
    EXPECT_EQ(windowOpenAction(WindowRole::Sender, WorkspaceMode::CNC, library),
              WindowOpenAction::Blocked);
    // Workshop/shared windows are unaffected by the overlay.
    EXPECT_EQ(windowOpenAction(WindowRole::Workshop, WorkspaceMode::Model, library),
              WindowOpenAction::Open);
}

TEST(WindowModePolicy, EveryCatalogEntryIsAvailableSomewhere) {
    for (const auto& entry : windowCatalogEntries()) {
        EXPECT_TRUE(windowAvailableInMode(entry.role, WorkspaceMode::Model) ||
                    windowAvailableInMode(entry.role, WorkspaceMode::CNC))
            << entry.key << " is reachable in no workspace mode";
    }
}

} // namespace dw
