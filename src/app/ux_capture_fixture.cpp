#include "app/ux_capture_fixture.h"

#include "app/project_session_integration.h"
#include "app/river_sign_study_fixture.h"
#include "core/config/config.h"
#include "core/library/library_manager.h"
#include "core/project/named_project_creation.h"
#include "core/project/project.h"
#include "core/project/project_asset_membership.h"

namespace dw {

std::optional<UxCaptureFixture>
seedUxCaptureFixture(LibraryManager& library,
                     ProjectManager& projects,
                     ProjectSessionIntegration& sessionIntegration,
                     std::string& error) {
    error.clear();
    const auto libraryFixture = river_sign_study::seedLibraryFixture(
        library, Path(DW_UX_FIXTURE_DIR));
    if (!libraryFixture.seeded()) {
        error = libraryFixture.error;
        return std::nullopt;
    }
    const i64 primaryId = libraryFixture.fixture.primaryId;
    const i64 alternateId = libraryFixture.fixture.alternateId;
    const i64 previewId = libraryFixture.fixture.previewOnlyId;

    NamedProjectCreationService creation(projects);
    const auto prepared = creation.prepare("River Sign");
    if (!prepared.prepared()) {
        error = "River Sign project could not be prepared";
        return std::nullopt;
    }
    const auto activated = sessionIntegration.activateProject(prepared.project);
    if (!activated.committed() ||
        creation.finish(prepared.token, true) != NamedProjectFinishStatus::Published) {
        error = "River Sign project could not be activated and published";
        return std::nullopt;
    }

    const auto active = projects.currentProject();
    if (!active) {
        error = "River Sign project lost active identity";
        return std::nullopt;
    }

    ProjectAssetMembershipService membership(projects);
    const auto ensured = membership.ensure(
        {active->id(), {{ProjectAssetKind::Model, primaryId}}});
    if (ensured.status == ProjectAssetMembershipStatus::Rejected) {
        error = "Primary model membership could not be created";
        return std::nullopt;
    }

    const auto primaryItem = projects.findOpenItemBySource("models", primaryId);
    if (!primaryItem || primaryItem->projectId != active->id()) {
        error = "Primary project item was not created";
        return std::nullopt;
    }
    if (!projects.save(*active)) {
        error = "River Sign project could not be saved";
        return std::nullopt;
    }
    Config::instance().addRecentProject(active->filePath());
    Config::instance().save();

    UxCaptureFixture fixture;
    fixture.project = workshop::ProjectId(active->id());
    fixture.primary = {workshop::LibraryItemKind::Model,
                       workshop::LibraryItemId(primaryId)};
    fixture.alternate = {workshop::LibraryItemKind::Model,
                         workshop::LibraryItemId(alternateId)};
    fixture.previewOnly = {workshop::LibraryItemKind::Model,
                           workshop::LibraryItemId(previewId)};
    fixture.primaryProjectItem = {
        fixture.project, workshop::ProjectItemId(primaryItem->id)};
    fixture.projectRoot = active->filePath();
    if (!fixture.valid()) {
        error = "River Sign fixture identities are incomplete";
        return std::nullopt;
    }
    return fixture;
}

} // namespace dw
