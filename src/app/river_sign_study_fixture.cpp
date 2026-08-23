#include "app/river_sign_study_fixture.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "core/library/library_manager.h"
#include "core/utils/file_utils.h"

namespace dw::river_sign_study {
namespace {

struct FixtureSpec {
    const char* filename;
    const char* displayName;
};

constexpr std::array<FixtureSpec, 3> FIXTURES{{
    {"river_sign_primary.stl", "Primary"},
    {"river_sign_alternate.stl", "Alternate"},
    {"river_sign_preview_only.stl", "Preview Only"},
}};

LibrarySeedResult failure(LibraryManager& library,
                          LibrarySeedStatus status,
                          std::string error,
                          const std::vector<i64>& importedIds) {
    bool rolledBack = true;
    for (auto item = importedIds.rbegin(); item != importedIds.rend(); ++item) {
        if (!library.removeModel(*item))
            rolledBack = false;
    }
    if (!rolledBack)
        error += "; imported fixture rollback was incomplete";
    return {status, {}, std::move(error)};
}

} // namespace

bool LibraryFixture::valid() const noexcept {
    return primaryId > 0 && alternateId > 0 && previewOnlyId > 0 &&
           primaryId != alternateId && primaryId != previewOnlyId &&
           alternateId != previewOnlyId;
}

std::array<i64, 3> LibraryFixture::modelIds() const noexcept {
    return {primaryId, alternateId, previewOnlyId};
}

std::array<std::string, 3> LibraryFixture::modelNames() const {
    return {"Primary", "Alternate", "Preview Only"};
}

LibrarySeedResult seedLibraryFixture(LibraryManager& library,
                                     const Path& fixtureDirectory) {
    if (library.modelCount() != 0) {
        return {LibrarySeedStatus::LibraryNotEmpty,
                {},
                "the study Library must be empty before fixture import"};
    }
    if (!file::isDirectory(fixtureDirectory)) {
        return {LibrarySeedStatus::FixtureDirectoryMissing,
                {},
                "fixture directory does not exist: " + fixtureDirectory.string()};
    }

    for (const auto& spec : FIXTURES) {
        const Path source = fixtureDirectory / spec.filename;
        if (!file::isFile(source)) {
            return {LibrarySeedStatus::FixtureFileMissing,
                    {},
                    "fixture file does not exist: " + source.string()};
        }
    }

    LibraryFixture fixture;
    std::vector<i64> importedIds;
    importedIds.reserve(FIXTURES.size());
    for (std::size_t index = 0; index < FIXTURES.size(); ++index) {
        const auto& spec = FIXTURES[index];
        const auto imported = library.importModel(fixtureDirectory / spec.filename);
        if (!imported.success || imported.modelId <= 0) {
            return failure(
                library,
                LibrarySeedStatus::ImportFailed,
                imported.error.empty() ? "fixture import failed" : imported.error,
                importedIds);
        }
        importedIds.push_back(imported.modelId);

        auto record = library.getModel(imported.modelId);
        if (!record) {
            return failure(library,
                           LibrarySeedStatus::VerificationFailed,
                           "imported fixture record is missing",
                           importedIds);
        }
        record->name = spec.displayName;
        if (!library.updateModel(*record)) {
            return failure(library,
                           LibrarySeedStatus::RenameFailed,
                           "fixture display name could not be persisted",
                           importedIds);
        }

        if (index == 0)
            fixture.primaryId = imported.modelId;
        else if (index == 1)
            fixture.alternateId = imported.modelId;
        else
            fixture.previewOnlyId = imported.modelId;
    }

    auto records = library.getAllModels();
    std::vector<std::string> actualNames;
    actualNames.reserve(records.size());
    for (const auto& record : records)
        actualNames.push_back(record.name);
    std::sort(actualNames.begin(), actualNames.end());
    const std::vector<std::string> expectedNames{
        "Alternate", "Preview Only", "Primary"};
    if (!fixture.valid() || library.modelCount() != 3 ||
        actualNames != expectedNames) {
        return failure(library,
                       LibrarySeedStatus::VerificationFailed,
                       "fixture import did not produce exactly the three named designs",
                       importedIds);
    }

    return {LibrarySeedStatus::Seeded, fixture, {}};
}

} // namespace dw::river_sign_study
