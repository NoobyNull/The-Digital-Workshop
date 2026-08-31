#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "modules/carve_preparation/preparation_identity.h"
#include "modules/carve_preparation/prepare_carve_flow.h"
#include "modules/carve_preparation/preparation_step_guidance.h"

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> carvePreparationSources() {
    const fs::path root = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "carve_preparation";
    std::vector<fs::path> sources;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".h" || extension == ".hpp" ||
                                        extension == ".cpp" || extension == ".cc")) {
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

TEST(CarvePreparationBoundary, IncludesNoUiPersistenceRendererOrMachineDependency) {
    const std::vector<std::string> forbidden = {
        "imgui",
        "OpenGL",
        "SDL",
        "ui/",
        "render/",
        "managers/",
        "app/",
        "core/",
        "database",
        "sqlite",
        "filesystem",
        "ProjectRepository",
        "ProjectManager",
        "cnc/",
        "CarveStreamer",
        "nlohmann",
    };

    const auto sources = carvePreparationSources();
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

TEST(CarvePreparationBoundary, TargetLinksOnlyTheSharedWorkshopContract) {
    const fs::path cmake = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "carve_preparation" /
                           "CMakeLists.txt";
    const auto contents = readFile(cmake);

    EXPECT_NE(contents.find("dw_workshop_core"), std::string::npos);
    for (const auto& token : {"imgui", "SDL", "OpenGL", "sqlite", "nlohmann_json"})
        EXPECT_EQ(contents.find(token), std::string::npos);
}

TEST(CarvePreparationBoundary, CommandsAreLimitedToPreparationRoutingIntents) {
    using namespace dw::carve_preparation;
    static_assert(std::variant_size_v<PreparationIdentityCommand> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, PreparationIdentityCommand>,
                                 RequestProjectCreation>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, PreparationIdentityCommand>,
                                 BeginPinnedPreparation>);
    SUCCEED();
}

TEST(CarvePreparationBoundary, FlowEffectsCannotExpressMachineOrStreamCommands) {
    using namespace dw::carve_preparation;
    static_assert(std::variant_size_v<PrepareCarveEffect> == 3);
    static_assert(std::is_same_v<std::variant_alternative_t<0, PrepareCarveEffect>,
                                 PreparationPreviewRequest>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, PrepareCarveEffect>,
                                 PreparationSaveRequest>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, PrepareCarveEffect>,
                                 PreparationReady>);
    SUCCEED();
}

TEST(CarvePreparationBoundary, StepViewsCanEmitOnlyTypedPreparationIntents) {
    using namespace dw::carve_preparation;
    static_assert(std::variant_size_v<PreparationStepIntent> == 5);
    static_assert(std::is_same_v<std::variant_alternative_t<0, PreparationStepIntent>,
                                 OpenPreparationField>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, PreparationStepIntent>,
                                 UpdatePreparationField>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, PreparationStepIntent>,
                                 SelectPreparationOption>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, PreparationStepIntent>,
                                 RequestStepPreviewGeneration>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, PreparationStepIntent>,
                                 TogglePreparationAdvanced>);
    SUCCEED();
}

TEST(CarvePreparationBoundary, ProductionFilesStayWithinFiveHundredLineTarget) {
    const auto sources = carvePreparationSources();
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
