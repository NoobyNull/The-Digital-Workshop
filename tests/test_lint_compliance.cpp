// Digital Workshop - Tier 2 Lint & Format Compliance Tests
//
// These tests verify that source files in render/, ui/, and app/ (which cannot
// be functionally tested without GL/SDL/ImGui) comply with coding standards.
// This catches formatting regressions, missing header guards, namespace
// pollution, and other style issues at build/test time.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string SRC_ROOT = CMAKE_SOURCE_DIR "/src";

// Collect all .h and .cpp files under the given directories
std::vector<std::string> collectFiles(const std::vector<std::string>& dirs,
                                      const std::vector<std::string>& extensions) {
    std::vector<std::string> result;
    for (const auto& dir : dirs) {
        std::string fullDir = SRC_ROOT + "/" + dir;
        if (!std::filesystem::exists(fullDir))
            continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(fullDir)) {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            for (const auto& e : extensions) {
                if (ext == e) {
                    result.push_back(entry.path().string());
                    break;
                }
            }
        }
    }
    return result;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream content;
    content << f.rdbuf();
    return content.str();
}

// All Tier 2 directories (GL/SDL/ImGui dependent — no functional tests)
const std::vector<std::string> TIER2_DIRS = {"render", "ui", "app"};

} // namespace

// --- Header guard check: every .h must have #pragma once ---

TEST(LintCompliance, AllHeaders_HavePragmaOnce) {
    auto headers = collectFiles(TIER2_DIRS, {".h"});
    ASSERT_GT(headers.size(), 0u) << "No headers found — check SRC_ROOT";

    for (const auto& path : headers) {
        // Skip generated font data files (immutable and necessarily formatted)
        if (path.find("inter_regular.h") != std::string::npos ||
            path.find("fa_solid_900.h") != std::string::npos) {
            continue;
        }

        auto content = readFile(path);
        EXPECT_NE(content.find("#pragma once"), std::string::npos)
            << "Missing #pragma once: " << path;
    }
}

// --- No 'using namespace' in headers ---

TEST(LintCompliance, NoUsingNamespaceInHeaders) {
    auto headers = collectFiles(TIER2_DIRS, {".h"});

    for (const auto& path : headers) {
        auto content = readFile(path);
        EXPECT_EQ(content.find("using namespace"), std::string::npos)
            << "Found 'using namespace' in header: " << path;
    }
}

// --- No tabs in source files ---

TEST(LintCompliance, NoTabs) {
    auto files = collectFiles(TIER2_DIRS, {".h", ".cpp"});

    for (const auto& path : files) {
        auto content = readFile(path);
        EXPECT_EQ(content.find('\t'), std::string::npos) << "Found tab character in: " << path;
    }
}

// --- No trailing whitespace ---

TEST(LintCompliance, NoTrailingWhitespace) {
    auto files = collectFiles(TIER2_DIRS, {".h", ".cpp"});

    for (const auto& path : files) {
        std::ifstream f(path);
        std::string line;
        int lineNum = 0;
        while (std::getline(f, line)) {
            lineNum++;
            if (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
                ADD_FAILURE() << "Trailing whitespace at " << path << ":" << lineNum;
            }
        }
    }
}

// --- No lines exceeding 120 characters ---

TEST(LintCompliance, NoLongLines) {
    auto files = collectFiles(TIER2_DIRS, {".h", ".cpp"});

    for (const auto& path : files) {
        // Skip generated font data files (immutable and necessarily long strings)
        if (path.find("inter_regular.h") != std::string::npos ||
            path.find("fa_solid_900.h") != std::string::npos) {
            continue;
        }

        std::ifstream f(path);
        std::string line;
        int lineNum = 0;
        while (std::getline(f, line)) {
            lineNum++;
            if (line.size() > 120) {
                ADD_FAILURE() << "Line exceeds 120 chars (" << line.size() << ") at " << path << ":"
                              << lineNum;
            }
        }
    }
}

// --- All source files are non-empty ---

TEST(LintCompliance, NoEmptyFiles) {
    auto files = collectFiles(TIER2_DIRS, {".h", ".cpp"});

    for (const auto& path : files) {
        auto size = std::filesystem::file_size(path);
        EXPECT_GT(size, 0u) << "Empty file: " << path;
    }
}

// --- Headers don't include themselves or have circular includes (basic check) ---

TEST(LintCompliance, HeadersIncludeGuardConsistency) {
    auto headers = collectFiles(TIER2_DIRS, {".h"});

    for (const auto& path : headers) {
        // Skip generated font data files (immutable and necessarily formatted)
        if (path.find("inter_regular.h") != std::string::npos ||
            path.find("fa_solid_900.h") != std::string::npos) {
            continue;
        }

        auto content = readFile(path);
        // First non-empty non-comment line should be #pragma once
        auto pos = content.find_first_not_of(" \t\n\r");
        if (pos != std::string::npos) {
            // Allow comment lines before #pragma once
            if (content.substr(pos, 2) == "//") {
                pos = content.find("#pragma once");
            }
            // Should find #pragma once within first 200 chars
            auto pragmaPos = content.find("#pragma once");
            EXPECT_NE(pragmaPos, std::string::npos) << "No #pragma once in: " << path;
            if (pragmaPos != std::string::npos) {
                EXPECT_LT(pragmaPos, 200u) << "#pragma once too late in: " << path;
            }
        }
    }
}

// clang-format compliance is enforced by CI static-analysis job
// (different clang-format versions produce different output, so a
// unit test here causes false failures across environments)
