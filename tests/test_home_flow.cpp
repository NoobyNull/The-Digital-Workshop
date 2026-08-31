#include <gtest/gtest.h>

#include "modules/workshop/home_flow.h"

namespace dw::workshop {
namespace {

TEST(HomeFlow, BeginOnlyOpensDraftAndDoesNotRequestPersistence) {
    HomeFlow flow;

    const auto result = flow.dispatch(BeginNamedProject{});

    EXPECT_EQ(result.status, HomeTransitionStatus::Applied);
    EXPECT_TRUE(result.snapshot.namingProject);
    EXPECT_FALSE(result.snapshot.submittingProject);
    EXPECT_FALSE(result.snapshot.canSubmitProject());
    EXPECT_FALSE(result.createRequest.has_value());
}

TEST(HomeFlow, BlankNameCannotRequestCreation) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"   "});

    const auto result = flow.dispatch(SubmitNamedProject{});

    EXPECT_EQ(result.status, HomeTransitionStatus::Rejected);
    EXPECT_FALSE(result.createRequest.has_value());
    EXPECT_EQ(result.snapshot.validationMessage, "Enter a project name.");
}

TEST(HomeFlow, SubmitTrimsNameAndEmitsOneCreateRequest) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"  River Sign  "});

    const auto first = flow.dispatch(SubmitNamedProject{});
    const auto second = flow.dispatch(SubmitNamedProject{});

    ASSERT_TRUE(first.createRequest.has_value());
    EXPECT_EQ(first.status, HomeTransitionStatus::CreateRequested);
    EXPECT_EQ(first.createRequest->name, "River Sign");
    EXPECT_TRUE(first.snapshot.submittingProject);
    EXPECT_EQ(second.status, HomeTransitionStatus::Rejected);
    EXPECT_FALSE(second.createRequest.has_value());
}

TEST(HomeFlow, RepeatedBeginPreservesTheVisibleDraftAndOutstandingRequest) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"River Sign"});

    const auto whileEditing = flow.dispatch(BeginNamedProject{});

    EXPECT_EQ(whileEditing.status, HomeTransitionStatus::Unchanged);
    EXPECT_EQ(whileEditing.snapshot.projectName, "River Sign");

    flow.dispatch(SubmitNamedProject{});
    const auto whileSubmitting = flow.dispatch(BeginNamedProject{});
    EXPECT_EQ(whileSubmitting.status, HomeTransitionStatus::Unchanged);
    EXPECT_EQ(whileSubmitting.snapshot.projectName, "River Sign");
    EXPECT_TRUE(whileSubmitting.snapshot.submittingProject);
}

TEST(HomeFlow, PathSeparatorsAreRejected) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"River/Sign"});

    const auto result = flow.dispatch(SubmitNamedProject{});

    EXPECT_EQ(result.status, HomeTransitionStatus::Rejected);
    EXPECT_FALSE(result.createRequest.has_value());
    EXPECT_EQ(result.snapshot.validationMessage,
              "Project names cannot contain path separators.");
}

TEST(HomeFlow, FailureUnlocksDraftAndPreservesNameForRetry) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"River Sign"});
    flow.dispatch(SubmitNamedProject{});

    const auto failed = flow.dispatch(CompleteNamedProject{false, "Storage is unavailable."});

    EXPECT_TRUE(failed.snapshot.namingProject);
    EXPECT_FALSE(failed.snapshot.submittingProject);
    EXPECT_EQ(failed.snapshot.projectName, "River Sign");
    EXPECT_EQ(failed.snapshot.validationMessage, "Storage is unavailable.");
    EXPECT_EQ(flow.dispatch(EditProjectName{"River Sign 2"}).status,
              HomeTransitionStatus::Applied);
    EXPECT_TRUE(flow.dispatch(SubmitNamedProject{}).createRequest.has_value());
}

TEST(HomeFlow, SuccessClosesAndClearsDraft) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"River Sign"});
    flow.dispatch(SubmitNamedProject{});

    const auto completed = flow.dispatch(CompleteNamedProject{true, {}});

    EXPECT_EQ(completed.status, HomeTransitionStatus::Applied);
    EXPECT_FALSE(completed.snapshot.namingProject);
    EXPECT_FALSE(completed.snapshot.submittingProject);
    EXPECT_TRUE(completed.snapshot.projectName.empty());
}

TEST(HomeFlow, CancelClearsDraftBeforeSubmission) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"River Sign"});

    const auto result = flow.dispatch(CancelNamedProject{});

    EXPECT_EQ(result.status, HomeTransitionStatus::Applied);
    EXPECT_FALSE(result.snapshot.namingProject);
    EXPECT_TRUE(result.snapshot.projectName.empty());
}

TEST(HomeFlow, CancelCannotRaceAnOutstandingCreateRequest) {
    HomeFlow flow;
    flow.dispatch(BeginNamedProject{});
    flow.dispatch(EditProjectName{"River Sign"});
    flow.dispatch(SubmitNamedProject{});

    const auto result = flow.dispatch(CancelNamedProject{});

    EXPECT_EQ(result.status, HomeTransitionStatus::Rejected);
    EXPECT_TRUE(result.snapshot.namingProject);
    EXPECT_TRUE(result.snapshot.submittingProject);
}

} // namespace
} // namespace dw::workshop
