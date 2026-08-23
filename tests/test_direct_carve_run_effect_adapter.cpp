#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include "app/direct_carve_run_effect_adapter.h"
#include "core/carve/carve_job.h"
#include "core/cnc/cnc_controller.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/job_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/mesh/hash.h"
#include "core/project/project.h"
#include "core/utils/file_utils.h"
#include "modules/project_session/project_session.h"

namespace dw {
namespace {

using namespace run_coordination;

class DirectCarveRunEffectAdapterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(Schema::initialize(m_database));

        m_manager = std::make_unique<ProjectManager>(m_database);
        m_jobs = std::make_unique<JobRepository>(m_database);
        m_projects = std::make_unique<ProjectRepository>(m_database);
        m_gcodes = std::make_unique<GCodeRepository>(m_database);

        m_project = m_manager->create("Protected Run Adapter");
        ASSERT_TRUE(m_project);
        m_manager->synchronizeActiveProject(m_project);

        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        m_gcodePath = std::filesystem::temp_directory_path() /
                      ("dw-run-effect-" + std::to_string(nonce) + ".nc");
        ASSERT_TRUE(file::writeText(m_gcodePath, "G21\nG90\nG0 X0 Y0\nM30\n"));
        m_fingerprint = hash::computeFile(m_gcodePath);
        ASSERT_FALSE(m_fingerprint.empty());

        ModelRepository models(m_database);
        ModelRecord model;
        model.name = "Relief";
        model.hash = "model-hash";
        model.filePath = "/tmp/relief.stl";
        model.fileFormat = "stl";
        m_modelSourceId = models.insert(model).value();
        ASSERT_TRUE(m_projects->addModel(m_project->id(), m_modelSourceId));

        m_groupId = m_gcodes->createGroup(m_modelSourceId, "Direct Carve", 0).value();
        GCodeRecord gcode;
        gcode.name = "Relief Run";
        gcode.hash = m_fingerprint;
        gcode.filePath = m_gcodePath;
        gcode.fileSize = file::fileSize(m_gcodePath);
        m_gcodeSourceId = m_gcodes->insert(gcode).value();
        ASSERT_TRUE(m_gcodes->addToGroup(m_groupId, m_gcodeSourceId));
        ASSERT_TRUE(m_gcodes->addToProject(m_project->id(), m_gcodeSourceId));

        const auto items = m_manager->listOpenItems(m_project->id());
        for (const auto& item : items) {
            if (item.itemType == ProjectOpenItemType::Model &&
                item.sourceId == m_modelSourceId) {
                m_modelItemId = item.id;
            } else if (item.itemType == ProjectOpenItemType::Operation &&
                       item.sourceId == m_groupId) {
                m_operationItemId = item.id;
            } else if (item.itemType == ProjectOpenItemType::Gcode &&
                       item.sourceId == m_gcodeSourceId) {
                m_gcodeItemId = item.id;
            }
        }
        ASSERT_GT(m_modelItemId, 0);
        ASSERT_GT(m_operationItemId, 0);
        ASSERT_GT(m_gcodeItemId, 0);

        ASSERT_EQ(send(workshop::ActivateProject{workshop::ProjectId(m_project->id())}).status,
                  workshop::TransitionStatus::Applied);
        ASSERT_EQ(send(workshop::SelectProjectItem{operationRef()}).status,
                  workshop::TransitionStatus::Applied);

        m_adapter = std::make_unique<DirectCarveRunEffectAdapter>(
            m_session, *m_manager, *m_jobs, m_carveJob, m_controller);
    }

    void TearDown() override {
        m_controller.disconnect();
        (void)file::remove(m_gcodePath);
    }

    workshop::WorkshopTransition send(workshop::WorkshopCommandPayload payload) {
        return m_session.dispatch({std::move(payload), std::nullopt});
    }

    workshop::ProjectItemRef modelRef() const {
        return {workshop::ProjectId(m_project->id()),
                workshop::ProjectItemId(m_modelItemId)};
    }

    workshop::ProjectItemRef operationRef() const {
        return {workshop::ProjectId(m_project->id()),
                workshop::ProjectItemId(m_operationItemId)};
    }

    workshop::ProjectItemRef gcodeRef() const {
        return {workshop::ProjectId(m_project->id()),
                workshop::ProjectItemId(m_gcodeItemId)};
    }

    RunPackage package(std::string fingerprint = {}) const {
        if (fingerprint.empty())
            fingerprint = m_fingerprint;
        carve_preparation::PrepareCarvePin pin(
            workshop::ProjectId(m_project->id()),
            modelRef(),
            {workshop::LibraryItemKind::Model,
             workshop::LibraryItemId(m_modelSourceId)},
            operationRef(),
            carve_preparation::PreparationToken{9},
            carve_preparation::PreparationRevision{11});
        RunSetupIdentity setup(
            pin,
            ToolpathIdentity(operationRef(),
                             gcodeRef(),
                             ToolpathRevision{13},
                             std::move(fingerprint)));
        RunIdentity identity(workshop::RunId{17}, setup);
        return RunPackage(identity,
                          RunPreflightSnapshot(setup,
                                               PreflightRevision{19},
                                               RunPreflightFacts::allSatisfied()));
    }

    void makeStreamableCarveJob() {
        carve::ToolpathConfig config;
        config.axis = carve::ScanAxis::XOnly;
        config.direction = carve::MillDirection::Alternating;
        config.customStepoverPct = 100.0f;
        config.scanResolutionMm = 5.0f;
        config.stepdownMm = 10.0f;

        VtdbToolGeometry tool;
        tool.id = "finish";
        tool.tool_type = VtdbToolType::EndMill;
        tool.units = VtdbUnits::Metric;
        tool.diameter = 5.0;
        m_carveJob.generateFixedDepthToolpath(Vec3{0.0f, 0.0f, 0.0f},
                                              Vec3{30.0f, 20.0f, 0.0f},
                                              Vec3{5.0f, 5.0f, 0.0f},
                                              Vec3{25.0f, 15.0f, 0.0f},
                                              2.0f,
                                              config,
                                              tool);
        ASSERT_EQ(m_carveJob.state(), carve::CarveJobState::Ready);
    }

    void connectSimulator() {
        ASSERT_TRUE(m_controller.connectSimulator());
        for (int attempts = 0; attempts < 100 && !m_controller.isConnected(); ++attempts)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ASSERT_TRUE(m_controller.isConnected());
    }

    Database m_database;
    workshop::ProjectSession m_session;
    CncController m_controller{nullptr};
    carve::CarveJob m_carveJob;
    std::unique_ptr<ProjectManager> m_manager;
    std::unique_ptr<JobRepository> m_jobs;
    std::unique_ptr<ProjectRepository> m_projects;
    std::unique_ptr<GCodeRepository> m_gcodes;
    std::unique_ptr<DirectCarveRunEffectAdapter> m_adapter;
    std::shared_ptr<Project> m_project;
    Path m_gcodePath;
    std::string m_fingerprint;
    i64 m_modelSourceId = 0;
    i64 m_groupId = 0;
    i64 m_gcodeSourceId = 0;
    i64 m_modelItemId = 0;
    i64 m_operationItemId = 0;
    i64 m_gcodeItemId = 0;
};

TEST_F(DirectCarveRunEffectAdapterTest, AcquiresOnlyTheExactProjectOperationLock) {
    const auto run = package();
    const auto acquired = m_adapter->execute(AcquireRunLock{run.identity()});

    EXPECT_TRUE(acquired.succeeded());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Locked);
    ASSERT_TRUE(m_session.snapshot().activeRun.has_value());
    EXPECT_EQ(m_session.snapshot().activeRun->run, run.identity().run());
    EXPECT_EQ(m_session.snapshot().activeRun->projectOperation, operationRef());

    RunPackage other = package("different-package-fingerprint");
    const auto conflict = m_adapter->execute(AcquireRunLock{other.identity()});
    EXPECT_EQ(conflict.error, DirectCarveRunEffectError::ActiveRunConflict);
    EXPECT_EQ(m_session.snapshot().activeRun->projectOperation, operationRef());
}

TEST_F(DirectCarveRunEffectAdapterTest, ChangedResolvedFileCannotStartOrCreateJob) {
    const auto run = package();
    ASSERT_TRUE(m_adapter->execute(AcquireRunLock{run.identity()}).succeeded());
    ASSERT_TRUE(file::writeText(m_gcodePath, "G21\nG90\nG1 X99\nM30\n"));

    const auto started = m_adapter->execute(StartStream{run});

    EXPECT_EQ(started.error, DirectCarveRunEffectError::GCodeFingerprintMismatch);
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Locked);
    EXPECT_FALSE(m_controller.isStreaming());
    EXPECT_TRUE(m_jobs->findRecent().empty());

    EXPECT_TRUE(m_adapter->execute(AbortStream{run.identity()}).succeeded());
    const auto released = m_adapter->execute(
        ReleaseRunLock{run.identity(), RunOutcome::Failed});
    EXPECT_TRUE(released.succeeded());
    EXPECT_FALSE(m_session.snapshot().activeRun.has_value());
}

TEST_F(DirectCarveRunEffectAdapterTest, RealStreamUsesExactGCodeAndSafetyControls) {
    makeStreamableCarveJob();
    connectSimulator();
    const auto run = package();
    ASSERT_TRUE(m_adapter->execute(AcquireRunLock{run.identity()}).succeeded());

    const auto started = m_adapter->execute(StartStream{run});
    ASSERT_TRUE(started.succeeded());
    ASSERT_TRUE(started.jobId.has_value());
    const auto job = m_jobs->findById(*started.jobId);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->filePath, m_gcodePath.string());
    EXPECT_EQ(job->status, "running");
    EXPECT_GT(job->totalLines, 0);

    EXPECT_TRUE(m_adapter->execute(FeedHold{run.identity()}).succeeded());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Paused);
    EXPECT_TRUE(m_adapter->execute(CycleStart{run.identity()}).succeeded());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Streaming);
    EXPECT_TRUE(m_adapter->execute(AbortStream{run.identity()}).succeeded());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Aborting);

    const auto unsafeResume = m_adapter->execute(CycleStart{run.identity()});
    EXPECT_EQ(unsafeResume.error, DirectCarveRunEffectError::InvalidControlState);
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Aborting);

    const auto released = m_adapter->execute(
        ReleaseRunLock{run.identity(), RunOutcome::Aborted});
    ASSERT_TRUE(released.succeeded());
    EXPECT_FALSE(m_session.snapshot().activeRun.has_value());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Idle);

    const auto finished = m_jobs->findById(*started.jobId);
    ASSERT_TRUE(finished.has_value());
    EXPECT_EQ(finished->status, "aborted");
    EXPECT_GE(finished->lastAckedLine, 0);

    const auto history = m_manager->listOpenItems(m_project->id());
    const auto historyItem = std::find_if(history.begin(), history.end(), [&](const auto& item) {
        return item.itemType == ProjectOpenItemType::Job &&
               item.sourceId == *started.jobId;
    });
    ASSERT_NE(historyItem, history.end());
    EXPECT_EQ(historyItem->parentItemId, m_gcodeItemId);
    EXPECT_EQ(historyItem->status, ProjectOpenItemStatus::Stale);
}

TEST_F(DirectCarveRunEffectAdapterTest, FailedStreamCanFinalizeHistoryAndReleaseExactLock) {
    makeStreamableCarveJob();
    const auto run = package();
    ASSERT_TRUE(m_adapter->execute(AcquireRunLock{run.identity()}).succeeded());

    const auto started = m_adapter->execute(StartStream{run});
    EXPECT_EQ(started.error, DirectCarveRunEffectError::StreamStartFailed);
    ASSERT_TRUE(started.jobId.has_value());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Streaming);

    EXPECT_TRUE(m_adapter->execute(AbortStream{run.identity()}).succeeded());
    const auto released = m_adapter->execute(
        ReleaseRunLock{run.identity(), RunOutcome::Failed});
    EXPECT_TRUE(released.succeeded());
    EXPECT_FALSE(m_session.snapshot().activeRun.has_value());
    const auto failedJob = m_jobs->findById(*started.jobId);
    ASSERT_TRUE(failedJob.has_value());
    EXPECT_EQ(failedJob->status, "interrupted");
}

TEST_F(DirectCarveRunEffectAdapterTest, MissingJobRowCannotStrandTheSafetyLock) {
    makeStreamableCarveJob();
    connectSimulator();
    const auto run = package();
    ASSERT_TRUE(m_adapter->execute(AcquireRunLock{run.identity()}).succeeded());
    ASSERT_TRUE(m_database.execute("DROP TABLE cnc_jobs"));

    const auto started = m_adapter->execute(StartStream{run});
    EXPECT_EQ(started.error, DirectCarveRunEffectError::JobRecordInsertFailed);
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Aborting);
    EXPECT_TRUE(m_session.snapshot().activeRun.has_value());

    EXPECT_TRUE(m_adapter->execute(AbortStream{run.identity()}).succeeded());
    const auto released = m_adapter->execute(
        ReleaseRunLock{run.identity(), RunOutcome::Failed});
    EXPECT_EQ(released.status, DirectCarveRunEffectStatus::Applied);
    EXPECT_EQ(released.error, DirectCarveRunEffectError::JobRecordUnavailable);
    EXPECT_FALSE(released.succeeded());
    EXPECT_FALSE(m_session.snapshot().activeRun.has_value());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Idle);
}

TEST_F(DirectCarveRunEffectAdapterTest, FailedJobFinalizationCannotStrandTheSafetyLock) {
    makeStreamableCarveJob();
    connectSimulator();
    const auto run = package();
    ASSERT_TRUE(m_adapter->execute(AcquireRunLock{run.identity()}).succeeded());
    ASSERT_TRUE(m_adapter->execute(StartStream{run}).succeeded());
    ASSERT_TRUE(m_database.execute("DROP TABLE cnc_jobs"));
    ASSERT_TRUE(m_adapter->execute(AbortStream{run.identity()}).succeeded());

    const auto released = m_adapter->execute(
        ReleaseRunLock{run.identity(), RunOutcome::Failed});

    EXPECT_EQ(released.status, DirectCarveRunEffectStatus::Applied);
    EXPECT_EQ(released.error, DirectCarveRunEffectError::JobFinalizeFailed);
    EXPECT_FALSE(released.succeeded());
    EXPECT_FALSE(m_session.snapshot().activeRun.has_value());
    EXPECT_EQ(m_adapter->snapshot().state, DirectCarveRunControlState::Idle);
}

} // namespace
} // namespace dw
