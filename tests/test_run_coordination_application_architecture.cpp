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
    const auto effects =
        readFile(root / "app" / "direct_carve_run_effect_adapter.cpp");

    EXPECT_NE(effects.find("WorkshopCommand{workshop::BeginRun"), std::string::npos);
    EXPECT_NE(effects.find("GCodeFingerprintMismatch"), std::string::npos);
    EXPECT_NE(effects.find("m_jobRepository.insert"), std::string::npos);
    EXPECT_NE(effects.find("m_jobRepository.finishJob"), std::string::npos);
    EXPECT_NE(effects.find("WorkshopCommand{workshop::EndRun"), std::string::npos);
    EXPECT_NE(effects.find("m_cncController.startStream(lines)"),
              std::string::npos);
}

TEST(RunCoordinationApplicationArchitecture, NewRunIntegrationUnitsRemainFocused) {
    const auto root = fs::path(CMAKE_SOURCE_DIR) / "src";
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
