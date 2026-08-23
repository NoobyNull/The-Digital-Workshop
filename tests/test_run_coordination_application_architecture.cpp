#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::size_t lineCount(const fs::path& path) {
    std::ifstream input(path);
    return static_cast<std::size_t>(std::count(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(), '\n'));
}

} // namespace

TEST(RunCoordinationApplicationArchitecture, StartUsesLockThenRealStreamAndExactHistory) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto panel =
        readFile(root / "ui" / "panels" / "direct_carve_run_adapter.cpp");
    const auto effects =
        readFile(root / "app" / "direct_carve_run_effect_adapter.cpp");
    const auto carveJob = readFile(root / "core" / "carve" / "carve_job.cpp");

    EXPECT_NE(panel.find("saveGCodeToProjectDirectory"), std::string::npos);
    EXPECT_NE(panel.find("RunPreflightSnapshot"), std::string::npos);
    EXPECT_NE(panel.find("m_runCoordinator.dispatch(StartRun"), std::string::npos);
    EXPECT_NE(effects.find("WorkshopCommand{workshop::BeginRun"), std::string::npos);
    EXPECT_NE(effects.find("GCodeFingerprintMismatch"), std::string::npos);
    EXPECT_NE(effects.find("m_jobRepository.insert"), std::string::npos);
    EXPECT_NE(effects.find("m_jobRepository.finishJob"), std::string::npos);
    EXPECT_NE(effects.find("WorkshopCommand{workshop::EndRun"), std::string::npos);
    EXPECT_NE(carveJob.find("controller->startStream(program)"), std::string::npos);
}

TEST(RunCoordinationApplicationArchitecture, UiControlsEmitOnlyCoordinatorCommands) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto running =
        readFile(root / "ui" / "panels" / "direct_carve_active_run.cpp");
    const auto preparation = readFile(
        root / "modules" / "carve_preparation" / "prepare_carve_flow.h");

    for (const auto& forbidden : {"feedHold", "cycleStart", "softReset",
                                  "startStreaming", "startStream"}) {
        EXPECT_EQ(running.find(forbidden), std::string::npos) << forbidden;
        EXPECT_EQ(preparation.find(forbidden), std::string::npos) << forbidden;
    }
    EXPECT_NE(running.find("requestRunPause"), std::string::npos);
    EXPECT_NE(running.find("requestRunResume"), std::string::npos);
    EXPECT_NE(running.find("requestRunAbort"), std::string::npos);
}

TEST(RunCoordinationApplicationArchitecture, ApplicationForwardsEveryTerminalMachineEvent) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    const auto wiring =
        readFile(root / "app" / "application_wiring_cnc.cpp");

    EXPECT_NE(wiring.find("DirectCarveRunEffectAdapter"), std::string::npos);
    EXPECT_NE(wiring.find("setRunEffectExecutor"), std::string::npos);
    EXPECT_NE(wiring.find("onRunProgress(progress)"), std::string::npos);
    EXPECT_NE(wiring.find("ControllerDisconnected"), std::string::npos);
    EXPECT_NE(wiring.find("ControllerAlarm"), std::string::npos);
    EXPECT_NE(wiring.find("InvalidMachineResponse"), std::string::npos);
    EXPECT_NE(wiring.find("OperatorEmergencyStop"), std::string::npos);
}

TEST(RunCoordinationApplicationArchitecture,
     MissingRunEffectExecutorPreservesPreviewAndExportButGuardsStartBeforeDispatch) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src" / "ui" / "panels";
    const auto navigation =
        readFile(root / "direct_carve_preparation_navigation.cpp");
    const auto runAdapter =
        readFile(root / "direct_carve_run_adapter.cpp");
    const auto preview =
        readFile(root / "direct_carve_carve_preview_step.cpp");
    const auto gcode =
        readFile(root / "direct_carve_project_gcode.cpp");
    const auto review =
        readFile(root / "direct_carve_review_run_step.cpp");

    // Missing application execution is a start-only gate. The early return
    // precedes RunCoordinator dispatch, so no lock or CNC effect can exist.
    EXPECT_NE(navigation.find(
                  "return m_stockSecuredConfirmed && m_runEffectExecutor"),
              std::string::npos);
    const auto unavailableGuard = runAdapter.find("if (!canStartCarve()");
    const auto startDispatch =
        runAdapter.find("m_runCoordinator.dispatch(StartRun");
    ASSERT_NE(unavailableGuard, std::string::npos);
    ASSERT_NE(startDispatch, std::string::npos);
    EXPECT_LT(unavailableGuard, startDispatch);

    // Planning and file output remain separate capabilities. Neither path
    // consults Run coordination or can emit one of its effects.
    EXPECT_NE(preview.find("requestPinnedPreviewGeneration()"), std::string::npos);
    EXPECT_NE(preview.find("saveGCodeToProject()"), std::string::npos);
    EXPECT_NE(gcode.find("void DirectCarvePanel::saveGCodeToProject()"),
              std::string::npos);
    for (const auto* source : {&preview, &gcode}) {
        EXPECT_EQ(source->find("m_runEffectExecutor"), std::string::npos);
        EXPECT_EQ(source->find("m_runCoordinator"), std::string::npos);
    }

    // Review disables only the final confirmation/start region. Export is
    // rendered before that gate and therefore stays usable without Run.
    const auto exportControl = review.find("Save as G-code");
    const auto startAvailability = review.find("const bool preflightIncomplete");
    ASSERT_NE(exportControl, std::string::npos);
    ASSERT_NE(startAvailability, std::string::npos);
    EXPECT_LT(exportControl, startAvailability);
    EXPECT_NE(review.find("!m_runEffectExecutor"), std::string::npos);
}

TEST(RunCoordinationApplicationArchitecture, NewRunIntegrationUnitsRemainFocused) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
    EXPECT_LE(lineCount(root / "ui" / "panels" / "direct_carve_run_adapter.cpp"),
              500U);
    EXPECT_LE(lineCount(root / "app" / "direct_carve_run_effect_adapter.cpp"),
              500U);
    EXPECT_LE(lineCount(root / "core" / "cnc" /
                        "cnc_controller_stream_control.cpp"),
              500U);
}

TEST(RunCoordinationApplicationArchitecture,
     CncControllerKeepsTransportParsingStreamingAndSimulationInOwnedUnits) {
    const auto cncRoot = fs::path(CMAKE_SOURCE_DIR) / "src" / "core" / "cnc";
    const auto controller = readFile(cncRoot / "cnc_controller.cpp");
    const auto parser = readFile(cncRoot / "cnc_controller_status_parser.cpp");
    const auto stream = readFile(cncRoot / "cnc_controller_stream_control.cpp");
    const auto simulatorRuntime =
        readFile(cncRoot / "cnc_controller_simulator_runtime.cpp");
    const auto simulatorCommands =
        readFile(cncRoot / "cnc_controller_simulator_commands.cpp");

    EXPECT_NE(controller.find("void CncController::ioThreadFunc()"),
              std::string::npos);
    EXPECT_EQ(controller.find("CncController::parseStatusReport"),
              std::string::npos);
    EXPECT_EQ(controller.find("CncController::simProcessCommand"),
              std::string::npos);
    EXPECT_NE(parser.find("CncController::parseStatusReport"),
              std::string::npos);
    EXPECT_NE(stream.find("CncController::startStream"), std::string::npos);
    EXPECT_NE(simulatorRuntime.find("CncController::simIoThreadFunc"),
              std::string::npos);
    EXPECT_NE(simulatorCommands.find("CncController::simProcessCommand"),
              std::string::npos);

    for (const auto& unit : {
             "cnc_controller.cpp",
             "cnc_controller_status_parser.cpp",
             "cnc_controller_stream_control.cpp",
             "cnc_controller_simulator_runtime.cpp",
             "cnc_controller_simulator_commands.cpp",
         }) {
        EXPECT_LE(lineCount(cncRoot / unit), 500U) << unit;
    }
}
