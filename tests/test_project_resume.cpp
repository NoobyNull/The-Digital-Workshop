#include <gtest/gtest.h>

#include "modules/workshop/project_resume.h"

namespace dw::workshop {
namespace {

class MemoryResumeStore final : public ProjectResumeStore {
  public:
    ProjectResumeLoadResult load() const override {
        return bookmark ? ProjectResumeLoadResult{ProjectResumeLoadStatus::Loaded, bookmark}
                        : ProjectResumeLoadResult{ProjectResumeLoadStatus::Missing, std::nullopt};
    }
    bool save(const ProjectResumeBookmark& value) override {
        ++saveCount;
        if (!saveSucceeds)
            return false;
        bookmark = value;
        return true;
    }
    bool clear() override {
        ++clearCount;
        if (!clearSucceeds)
            return false;
        bookmark.reset();
        return true;
    }

    std::optional<ProjectResumeBookmark> bookmark;
    bool saveSucceeds = true;
    bool clearSucceeds = true;
    int saveCount = 0;
    int clearCount = 0;
};

struct ResumeHarness {
    MemoryResumeStore store;
    ResumeProjectStatus projectStatus = ResumeProjectStatus::Ready;
    ResumeItemStatus itemStatus = ResumeItemStatus::Ready;
    ResumeActivationStatus projectActivation = ResumeActivationStatus::Applied;
    ResumeActivationStatus itemActivation = ResumeActivationStatus::Applied;
    int projectActivations = 0;
    int itemActivations = 0;
    int homeShows = 0;

    ProjectResumeCallbacks callbacks() {
        return {
            [this](const ProjectResumeBookmark&) { return projectStatus; },
            [this](ProjectId) {
                ++projectActivations;
                return projectActivation;
            },
            [this](ProjectItemRef) { return itemStatus; },
            [this](ProjectItemRef) {
                ++itemActivations;
                return itemActivation;
            },
            [this]() { ++homeShows; },
        };
    }
};

ProjectResumeBookmark riverSignBookmark(bool withItem = true) {
    return {ProjectId(42), withItem ? std::optional{ProjectItemId(7)} : std::nullopt};
}

TEST(ProjectResume, MissingBookmarkShowsHomeWithoutActivation) {
    ResumeHarness harness;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::NoBookmark);
    EXPECT_EQ(harness.homeShows, 1);
    EXPECT_EQ(harness.projectActivations, 0);
}

TEST(ProjectResume, InvalidProjectClearsBookmarkAndShowsHome) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    harness.projectStatus = ResumeProjectStatus::IdentityMismatch;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::InvalidBookmark);
    EXPECT_FALSE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.homeShows, 1);
    EXPECT_EQ(harness.projectActivations, 0);
}

TEST(ProjectResume, ValidProjectAndItemRestoreExactlyOnce) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::ProjectAndItemRestored);
    EXPECT_TRUE(result.projectRestored);
    EXPECT_TRUE(result.itemRestored);
    EXPECT_EQ(harness.projectActivations, 1);
    EXPECT_EQ(harness.itemActivations, 1);
    EXPECT_EQ(harness.homeShows, 0);
}

TEST(ProjectResume, StaleItemFallsBackToProjectAndClearsOnlyItem) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    harness.itemStatus = ResumeItemStatus::Stale;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::ProjectRestored);
    ASSERT_TRUE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.store.bookmark->project, ProjectId(42));
    EXPECT_FALSE(harness.store.bookmark->item.has_value());
    EXPECT_EQ(harness.projectActivations, 1);
    EXPECT_EQ(harness.itemActivations, 0);
}

TEST(ProjectResume, PendingItemLoadKeepsBookmarkWithoutClaimingContentRestored) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    harness.itemActivation = ResumeActivationStatus::Pending;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::ItemActivationPending);
    EXPECT_TRUE(result.projectRestored);
    EXPECT_FALSE(result.itemRestored);
    EXPECT_TRUE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.store.bookmark->item, ProjectItemId(7));
    EXPECT_EQ(harness.store.clearCount, 0);
}

TEST(ProjectResume, SupersededActivationDoesNotClearOrShowHomeOverNewerState) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    harness.projectActivation = ResumeActivationStatus::Superseded;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::ActivationSuperseded);
    EXPECT_TRUE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.homeShows, 0);
}

TEST(ProjectResume, RejectedActivationClearsBookmarkAndShowsHome) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    harness.projectActivation = ResumeActivationStatus::Rejected;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::ActivationRejected);
    EXPECT_FALSE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.homeShows, 1);
}

TEST(ProjectResume, RememberingNewProjectClearsPriorItem) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    ASSERT_TRUE(coordinator.rememberProject(ProjectId(99)));

    ASSERT_TRUE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.store.bookmark->project, ProjectId(99));
    EXPECT_FALSE(harness.store.bookmark->item.has_value());
}

TEST(ProjectResume, RememberItemRequiresMatchingProject) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark(false);
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    EXPECT_FALSE(coordinator.rememberItem(ProjectItemRef{ProjectId(99), ProjectItemId(8)}));
    EXPECT_FALSE(harness.store.bookmark->item.has_value());

    EXPECT_TRUE(coordinator.rememberItem(ProjectItemRef{ProjectId(42), ProjectItemId(8)}));
    ASSERT_TRUE(harness.store.bookmark->item.has_value());
    EXPECT_EQ(*harness.store.bookmark->item, ProjectItemId(8));
}

TEST(ProjectResume, ExplicitCloseClearsButApplicationExitRetainsBookmark) {
    ResumeHarness harness;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    harness.store.bookmark = riverSignBookmark();
    EXPECT_TRUE(coordinator.completeClose(ProjectClosePurpose::ApplicationExit));
    EXPECT_TRUE(harness.store.bookmark.has_value());

    EXPECT_TRUE(coordinator.completeClose(ProjectClosePurpose::ExplicitClose));
    EXPECT_FALSE(harness.store.bookmark.has_value());
}

TEST(ProjectResume, DestructionAlwaysClearsEvenDuringApplicationExit) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    EXPECT_TRUE(coordinator.completeClose(ProjectClosePurpose::ApplicationExit));
    EXPECT_TRUE(harness.store.bookmark.has_value());
    EXPECT_TRUE(coordinator.completeDestruction());
    EXPECT_FALSE(harness.store.bookmark.has_value());
}

TEST(ProjectResume, PersistenceFailuresAreReportedWithoutClaimingAStateChange) {
    ResumeHarness harness;
    harness.store.bookmark = riverSignBookmark();
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    harness.store.saveSucceeds = false;
    EXPECT_FALSE(coordinator.rememberProject(ProjectId(99)));
    ASSERT_TRUE(harness.store.bookmark.has_value());
    EXPECT_EQ(harness.store.bookmark->project, ProjectId(42));

    harness.store.clearSucceeds = false;
    EXPECT_FALSE(coordinator.completeClose(ProjectClosePurpose::ExplicitClose));
    EXPECT_TRUE(harness.store.bookmark.has_value());
}

TEST(ProjectResume, InvalidPersistedStateReportsFailedRepair) {
    ResumeHarness harness;
    harness.store.bookmark = ProjectResumeBookmark{ProjectId(-1), std::nullopt};
    harness.store.clearSucceeds = false;
    ProjectResumeCoordinator coordinator(harness.store, harness.callbacks());

    const auto result = coordinator.restore();

    EXPECT_EQ(result.status, ProjectResumeStatus::PersistenceFailure);
    EXPECT_FALSE(result.persistenceHealthy);
    EXPECT_EQ(harness.homeShows, 1);
    EXPECT_TRUE(harness.store.bookmark.has_value());
}

} // namespace
} // namespace dw::workshop
