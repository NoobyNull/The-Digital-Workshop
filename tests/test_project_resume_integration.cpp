#include <gtest/gtest.h>

#include <filesystem>

#include "app/project_session_integration.h"
#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/schema.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_resume.h"

namespace dw {
namespace {

class MemoryProjectResumeStore final : public workshop::ProjectResumeStore {
  public:
    workshop::ProjectResumeLoadResult load() const override {
        return value ? workshop::ProjectResumeLoadResult{
                           workshop::ProjectResumeLoadStatus::Loaded, value}
                     : workshop::ProjectResumeLoadResult{
                           workshop::ProjectResumeLoadStatus::Missing, std::nullopt};
    }
    bool save(const workshop::ProjectResumeBookmark& bookmark) override {
        value = bookmark;
        return true;
    }
    bool clear() override {
        value.reset();
        return true;
    }

    std::optional<workshop::ProjectResumeBookmark> value;
};

TEST(ProjectResumeIntegration, FreshSessionRestoresPersistedProjectAndOpenItem) {
    const Path root = std::filesystem::temp_directory_path() / "dw_resume_integration";
    std::filesystem::remove_all(root);
    const Path oldProjectsDir = Config::instance().getProjectsDir();
    Config::instance().setProjectsDir(root / "Projects");

    Database database;
    ASSERT_TRUE(database.open(":memory:"));
    ASSERT_TRUE(Schema::initialize(database));
    ProjectManager manager(database);

    auto storedProject = manager.create("River Sign", true);
    ASSERT_NE(storedProject, nullptr);
    ASSERT_TRUE(manager.save(*storedProject));

    ProjectOpenItem storedItem;
    storedItem.projectId = storedProject->id();
    // This integration owns project/session identity, not panel content. Use an
    // identity-only item so it cannot imply that a specific editor was opened.
    storedItem.itemType = ProjectOpenItemType::Tool;
    storedItem.sourceKey = "tool:river-sign-finish";
    storedItem.displayName = "River Sign Finish Tool";
    storedItem.status = ProjectOpenItemStatus::Ready;
    const auto itemId = manager.upsertOpenItem(storedItem);
    ASSERT_TRUE(itemId.has_value());

    workshop::ProjectSession session;
    ProjectSessionIntegration integration(session, manager);
    MemoryProjectResumeStore store;
    store.value = workshop::ProjectResumeBookmark{
        workshop::ProjectId(storedProject->id()), workshop::ProjectItemId(*itemId)};

    int homeShows = 0;
    workshop::ProjectResumeCallbacks callbacks;
    callbacks.inspectProject = [&manager](const workshop::ProjectResumeBookmark& bookmark) {
        switch (manager.validateProjectStorage(bookmark.project.value)) {
        case ProjectStorageValidationStatus::Ready:
            return workshop::ResumeProjectStatus::Ready;
        case ProjectStorageValidationStatus::MissingRecord:
            return workshop::ResumeProjectStatus::Missing;
        case ProjectStorageValidationStatus::IdentityMismatch:
            return workshop::ResumeProjectStatus::IdentityMismatch;
        default:
            return workshop::ResumeProjectStatus::InvalidStorage;
        }
    };
    callbacks.activateProject = [&manager, &integration](workshop::ProjectId projectId) {
        auto project = manager.open(projectId.value);
        if (!project)
            return workshop::ResumeActivationStatus::Rejected;
        return integration.activateProject(std::move(project)).committed()
                   ? workshop::ResumeActivationStatus::Applied
                   : workshop::ResumeActivationStatus::Rejected;
    };
    callbacks.inspectItem = [&manager](workshop::ProjectItemRef itemRef) {
        const auto item = manager.findOpenItem(itemRef.item.value);
        if (!item)
            return workshop::ResumeItemStatus::Missing;
        return item->projectId == itemRef.project.value ? workshop::ResumeItemStatus::Ready
                                                        : workshop::ResumeItemStatus::ForeignProject;
    };
    callbacks.activateItem = [&session](workshop::ProjectItemRef itemRef) {
        const auto transition = session.dispatch(
            workshop::WorkshopCommand{workshop::SelectProjectItem{itemRef}, std::nullopt});
        return transition.accepted() ? workshop::ResumeActivationStatus::Applied
                                     : workshop::ResumeActivationStatus::Rejected;
    };
    callbacks.showHome = [&homeShows]() { ++homeShows; };

    workshop::ProjectResumeCoordinator coordinator(store, std::move(callbacks));
    const auto restored = coordinator.restore();

    EXPECT_EQ(restored.status, workshop::ProjectResumeStatus::ProjectAndItemRestored);
    ASSERT_NE(manager.currentProject(), nullptr);
    EXPECT_EQ(manager.currentProject()->id(), storedProject->id());
    const auto context = session.snapshot();
    EXPECT_EQ(context.route, workshop::WorkshopRoute::Project);
    ASSERT_TRUE(context.activeProjectItem.has_value());
    EXPECT_EQ(context.activeProjectItem->item, workshop::ProjectItemId(*itemId));
    EXPECT_EQ(homeShows, 0);

    Config::instance().setProjectsDir(oldProjectsDir);
    std::filesystem::remove_all(root);
}

} // namespace
} // namespace dw
