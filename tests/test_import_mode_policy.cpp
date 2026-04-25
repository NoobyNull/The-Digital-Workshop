// Digital Workshop - Import Mode Policy Tests

#include <gtest/gtest.h>

#include "core/import/import_mode_policy.h"

using namespace dw;

TEST(ImportModePolicy, InitialModeUsesSavedPreferenceForLocalFiles) {
    EXPECT_EQ(import_mode_policy::initialModeFor(StorageLocation::Local,
                                                FileHandlingMode::LeaveInPlace),
              FileHandlingMode::LeaveInPlace);
    EXPECT_EQ(import_mode_policy::initialModeFor(StorageLocation::Local,
                                                FileHandlingMode::CopyToLibrary),
              FileHandlingMode::CopyToLibrary);
    EXPECT_EQ(import_mode_policy::initialModeFor(StorageLocation::Local,
                                                FileHandlingMode::MoveToLibrary),
              FileHandlingMode::MoveToLibrary);
}

TEST(ImportModePolicy, InitialModeDoesNotDefaultNetworkImportsToMove) {
    EXPECT_EQ(import_mode_policy::initialModeFor(StorageLocation::Network,
                                                FileHandlingMode::MoveToLibrary),
              FileHandlingMode::CopyToLibrary);
}

TEST(ImportModePolicy, InitialModePreservesNonMovePreferenceForNetworkFiles) {
    EXPECT_EQ(import_mode_policy::initialModeFor(StorageLocation::Network,
                                                FileHandlingMode::LeaveInPlace),
              FileHandlingMode::LeaveInPlace);
    EXPECT_EQ(import_mode_policy::initialModeFor(StorageLocation::Network,
                                                FileHandlingMode::CopyToLibrary),
              FileHandlingMode::CopyToLibrary);
}
