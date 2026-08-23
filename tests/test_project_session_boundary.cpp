#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> projectSessionSources() {
    const fs::path root =
        fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "project_session";
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

} // namespace

TEST(ProjectSessionBoundary, HasOnlyWorkshopAndStandardLibraryIncludes) {
    const std::vector<std::string> forbidden = {
        "imgui",
        "glad",
        "OpenGL",
        "nfd",
        "ui/",
        "render/",
        "managers/",
        "app/",
        "core/",
        "SDL",
        "GL/",
        "database",
        "filesystem",
    };

    const auto sources = projectSessionSources();
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

TEST(ProjectSessionBoundary, BuildTargetLinksOnlyWorkshopCore) {
    const fs::path cmakePath = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" /
                               "project_session" / "CMakeLists.txt";
    std::ifstream input(cmakePath);
    ASSERT_TRUE(input.is_open()) << cmakePath;
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());

    EXPECT_NE(contents.find("dw_workshop_core"), std::string::npos);
    for (const auto& token : {"imgui", "SDL", "OpenGL", "glad", "nfd", "sqlite"}) {
        EXPECT_EQ(contents.find(token), std::string::npos)
            << cmakePath << " contains forbidden dependency " << token;
    }
}

TEST(ProjectSessionBoundary, ProductionFilesStayWithinFiveHundredLineTarget) {
    const auto sources = projectSessionSources();
    ASSERT_FALSE(sources.empty());

    for (const auto& path : sources) {
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open()) << path;

        int lineCount = 0;
        std::string line;
        while (std::getline(input, line))
            ++lineCount;

        EXPECT_LE(lineCount, 500) << path;
    }
}
