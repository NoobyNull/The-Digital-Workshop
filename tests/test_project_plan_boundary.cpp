#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> projectPlanSources() {
    const fs::path root = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "project_plan";
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

TEST(ProjectPlanBoundary, HasNoPersistenceUiRendererOrHardwareDependency) {
    const std::vector<std::string> forbidden = {
        "imgui", "OpenGL", "SDL", "ui/", "render/", "managers/", "app/",
        "core/", "database", "sqlite", "filesystem", "ProjectRepository",
        "ProjectManager", "nlohmann", "intentJson", "snapshotJson",
    };

    const auto sources = projectPlanSources();
    ASSERT_FALSE(sources.empty());
    for (const auto& path : sources) {
        const auto contents = readFile(path);
        for (const auto& token : forbidden)
            EXPECT_EQ(contents.find(token), std::string::npos)
                << path << " contains forbidden dependency " << token;
    }
}

TEST(ProjectPlanBoundary, TargetLinksOnlyTheSharedWorkshopContract) {
    const fs::path cmake = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" /
                           "project_plan" / "CMakeLists.txt";
    const auto contents = readFile(cmake);

    EXPECT_NE(contents.find("dw_workshop_core"), std::string::npos);
    for (const auto& token : {"imgui", "SDL", "OpenGL", "sqlite", "nlohmann_json"})
        EXPECT_EQ(contents.find(token), std::string::npos);
}

TEST(ProjectPlanBoundary, ProductionFilesStayWithinFiveHundredLineTarget) {
    const auto sources = projectPlanSources();
    ASSERT_FALSE(sources.empty());
    for (const auto& path : sources) {
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open()) << path;
        int lines = 0;
        std::string line;
        while (std::getline(input, line)) ++lines;
        EXPECT_LE(lines, 500) << path;
    }
}
