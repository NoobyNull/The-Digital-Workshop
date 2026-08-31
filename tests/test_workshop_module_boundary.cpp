#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

bool isCppSource(const fs::path& path) {
    const auto extension = path.extension().string();
    return extension == ".h" || extension == ".hpp" || extension == ".cpp" || extension == ".cc";
}

std::vector<fs::path> workshopSources() {
    const fs::path root = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "workshop";
    std::vector<fs::path> sources;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && isCppSource(entry.path()))
            sources.push_back(entry.path());
    }
    return sources;
}

bool isPresentationSource(const fs::path& path) {
    const fs::path root = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "workshop";
    const fs::path relative = fs::relative(path, root);
    return !relative.empty() && *relative.begin() == "ui";
}

} // namespace

TEST(WorkshopModuleBoundary, HasNoUiRendererManagerOrPlatformIncludes) {
    const std::vector<std::string> forbidden = {
        "imgui",
        "glad",
        "OpenGL",
        "nfd",
        "ui/",
        "render/",
        "managers/",
        "app/",
        "core/types.h",
        "../",
        "SDL",
        "GL/",
    };

    const auto sources = workshopSources();
    ASSERT_FALSE(sources.empty());

    for (const auto& path : sources) {
        if (isPresentationSource(path))
            continue;
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

TEST(WorkshopModuleBoundary, BuildTargetDeclaresNoUiOrPlatformLibraries) {
    const fs::path cmakePath = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "workshop" /
                               "CMakeLists.txt";
    std::ifstream input(cmakePath);
    ASSERT_TRUE(input.is_open()) << cmakePath;

    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    const std::vector<std::string> forbidden = {
        "target_link_libraries",
        "dw_configure_target",
        "imgui",
        "SDL",
        "OpenGL",
        "glad",
        "nfd",
        "ui/",
    };

    for (const auto& token : forbidden) {
        EXPECT_EQ(contents.find(token), std::string::npos)
            << cmakePath << " contains forbidden build dependency " << token;
    }
}

TEST(WorkshopModuleBoundary, ContextBarConsumesOnlyTheImmutableShellSnapshot) {
    const fs::path uiRoot = fs::path(CMAKE_SOURCE_DIR) / "src" / "modules" / "workshop" / "ui";
    const std::vector<fs::path> sources = {
        uiRoot / "project_context_bar.h",
        uiRoot / "project_context_bar.cpp",
        uiRoot / "project_context_bar_model.h",
    };
    const std::vector<std::string> forbidden = {
        "ProjectManager",
        "ProjectSession",
        "CncController",
        "Repository",
        "core/database",
        "managers/",
    };

    for (const auto& path : sources) {
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open()) << path;
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        for (const auto& token : forbidden) {
            EXPECT_EQ(contents.find(token), std::string::npos)
                << path << " reaches around ProjectShellSnapshot through " << token;
        }
    }
}

TEST(WorkshopModuleBoundary, NewModuleFilesStayWithinFiveHundredLineTarget) {
    const auto sources = workshopSources();
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
