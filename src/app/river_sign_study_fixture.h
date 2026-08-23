#pragma once

#include <array>
#include <string>

#include "core/types.h"

namespace dw {

class LibraryManager;

namespace river_sign_study {

enum class LibrarySeedStatus {
    Seeded,
    LibraryNotEmpty,
    FixtureDirectoryMissing,
    FixtureFileMissing,
    ImportFailed,
    RenameFailed,
    VerificationFailed,
};

struct LibraryFixture {
    i64 primaryId = 0;
    i64 alternateId = 0;
    i64 previewOnlyId = 0;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::array<i64, 3> modelIds() const noexcept;
    [[nodiscard]] std::array<std::string, 3> modelNames() const;
};

struct LibrarySeedResult {
    LibrarySeedStatus status = LibrarySeedStatus::VerificationFailed;
    LibraryFixture fixture;
    std::string error;

    [[nodiscard]] bool seeded() const noexcept {
        return status == LibrarySeedStatus::Seeded && fixture.valid();
    }
};

// Imports only the three canonical Library designs. It never creates a
// project, selects a model, opens a route, or records study outcomes.
[[nodiscard]] LibrarySeedResult
seedLibraryFixture(LibraryManager& library, const Path& fixtureDirectory);

} // namespace river_sign_study
} // namespace dw
