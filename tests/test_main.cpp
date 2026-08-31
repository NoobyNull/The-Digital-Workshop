// Digital Workshop - Test Main

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

void setEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

int processId() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

} // namespace

int main(int argc, char** argv) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("dw-tests-process-" + std::to_string(processId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "home");
    setEnvironment("HOME", (root / "home").string());
    setEnvironment("XDG_CONFIG_HOME", (root / "config").string());
    setEnvironment("XDG_DATA_HOME", (root / "data").string());
    setEnvironment("XDG_CACHE_HOME", (root / "cache").string());

    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    std::filesystem::remove_all(root);
    return result;
}

// Placeholder test to verify test infrastructure works
TEST(Placeholder, TestFrameworkWorks) {
    EXPECT_EQ(1 + 1, 2);
}
