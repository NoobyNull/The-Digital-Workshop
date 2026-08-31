#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "modules/run_coordination/run_coordinator.h"

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> runCoordinationSources() {
    const fs::path root = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" /
                          "run_coordination";
    std::vector<fs::path> sources;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto extension = entry.path().extension().string();
        if (entry.is_regular_file() &&
            (extension == ".h" || extension == ".hpp" || extension == ".cpp" ||
             extension == ".cc")) {
            sources.push_back(entry.path());
        }
    }
    return sources;
}

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.is_open()) << path;
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(RunCoordinationBoundary, IncludesNoUiPersistenceRendererApplicationOrHardwareDependency) {
    const std::vector<std::string> forbidden = {
        "imgui",       "OpenGL",    "SDL",       "ui/",       "render/",
        "managers/",   "app/",      "core/",     "database",  "sqlite",
        "filesystem",  "Repository", "CarveJob",  "cnc/",      "CarveStreamer",
        "CncController", "nlohmann",
    };

    const auto sources = runCoordinationSources();
    ASSERT_FALSE(sources.empty());
    for (const auto& path : sources) {
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open()) << path;
        std::string line;
        int lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            if (line.find("#include") == std::string::npos)
                continue;
            for (const auto& token : forbidden) {
                EXPECT_EQ(line.find(token), std::string::npos)
                    << path << ':' << lineNumber << " includes forbidden dependency " << token;
            }
        }
    }
}

TEST(RunCoordinationBoundary, TargetLinksOnlyPureWorkshopAndPreparationContracts) {
    const fs::path cmake = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" /
                           "run_coordination" / "CMakeLists.txt";
    const auto contents = readFile(cmake);

    EXPECT_NE(contents.find("dw_carve_preparation"), std::string::npos);
    EXPECT_NE(contents.find("dw_workshop_core"), std::string::npos);
    for (const auto& token : {"imgui", "SDL", "OpenGL", "sqlite", "nlohmann_json"})
        EXPECT_EQ(contents.find(token), std::string::npos);
}

TEST(RunCoordinationBoundary, CoordinatorCommandsAndEffectsAreClosedTypedContracts) {
    using namespace dw::run_coordination;
    static_assert(std::variant_size_v<RunCommand> == 7);
    static_assert(std::is_same_v<std::variant_alternative_t<0, RunCommand>, StartRun>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, RunCommand>, PauseRun>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, RunCommand>, ResumeRun>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, RunCommand>, AbortRun>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, RunCommand>, RunProgressed>);
    static_assert(std::is_same_v<std::variant_alternative_t<5, RunCommand>, CompleteRun>);
    static_assert(std::is_same_v<std::variant_alternative_t<6, RunCommand>, FailRun>);

    static_assert(std::variant_size_v<RunEffect> == 6);
    static_assert(std::is_same_v<std::variant_alternative_t<0, RunEffect>, AcquireRunLock>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, RunEffect>, StartStream>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, RunEffect>, FeedHold>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, RunEffect>, CycleStart>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, RunEffect>, AbortStream>);
    static_assert(std::is_same_v<std::variant_alternative_t<5, RunEffect>, ReleaseRunLock>);
    SUCCEED();
}

TEST(RunCoordinationBoundary, PrepareCarveFlowCannotExpressRunOrMachineEffects) {
    const fs::path header = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" /
                            "carve_preparation" / "prepare_carve_flow.h";
    const auto contents = readFile(header);

    for (const auto& token : {"StartStream", "FeedHold", "CycleStart", "AbortStream",
                              "AcquireRunLock", "ReleaseRunLock"}) {
        EXPECT_EQ(contents.find(token), std::string::npos);
    }
}

TEST(RunCoordinationBoundary, ProductionFilesStayWithinFiveHundredLineTarget) {
    const auto sources = runCoordinationSources();
    ASSERT_FALSE(sources.empty());
    for (const auto& path : sources) {
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open()) << path;
        int lines = 0;
        std::string line;
        while (std::getline(input, line))
            ++lines;
        EXPECT_LE(lines, 500) << path;
    }
}
