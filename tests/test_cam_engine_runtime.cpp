#include "core/cam/cam_engine_runtime.h"
#include "test_env_util.h"
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace dw::cam {
namespace {

// Unique temp dir that cleans itself up.
class TempDir {
  public:
    TempDir() {
        static int counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("dw_cam_runtime_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" +
                  std::to_string(counter++));
        std::filesystem::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const Path& path() const { return m_path; }

  private:
    Path m_path;
};

void writeFile(const Path& path, const char* contents) {
    std::ofstream out(path);
    out << contents;
}

void makePayload(const Path& dir) {
    writeFile(dir / "dw-cam-engine.js", "// engine\n");
    // manifold-3d ships unbundled beside the engine; completeness requires it.
    std::filesystem::create_directories(dir / "node_modules" / "manifold-3d");
    writeFile(dir / "bun", "#!/bin/sh\n");
    std::filesystem::permissions(dir / "bun",
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
}

} // namespace

TEST(CamEngineRuntime, BuildsLoopbackBaseUrl) {
    EXPECT_EQ(baseUrl(CamEngineConfig{}), "http://127.0.0.1:8973");

    CamEngineConfig custom;
    custom.host = "localhost";
    custom.port = 18990;
    EXPECT_EQ(baseUrl(custom), "http://localhost:18990");
}

TEST(CamEngineRuntime, PayloadCompletenessRequiresBothFiles) {
    TempDir missing;
    EXPECT_FALSE(payloadLooksComplete(missing.path() / "nope"));
    EXPECT_FALSE(payloadLooksComplete(missing.path()));

    TempDir scriptOnly;
    writeFile(scriptOnly.path() / "dw-cam-engine.js", "// engine\n");
    EXPECT_FALSE(payloadLooksComplete(scriptOnly.path()));

    TempDir noManifold;
    writeFile(noManifold.path() / "dw-cam-engine.js", "// engine\n");
    writeFile(noManifold.path() / "bun", "#!/bin/sh\n");
    std::filesystem::permissions(noManifold.path() / "bun",
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
    EXPECT_FALSE(payloadLooksComplete(noManifold.path()));

    TempDir notExecutable;
    makePayload(notExecutable.path());
    std::filesystem::permissions(notExecutable.path() / "bun",
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::remove);
    EXPECT_FALSE(payloadLooksComplete(notExecutable.path()));

    TempDir complete;
    makePayload(complete.path());
    EXPECT_TRUE(payloadLooksComplete(complete.path()));
}

TEST(CamEngineRuntime, StatusFactoriesCarryDistinctReasons) {
    auto ready = CamEngineStatus::ok("http://127.0.0.1:8973");
    EXPECT_TRUE(ready.ready);
    EXPECT_EQ(ready.endpoint, "http://127.0.0.1:8973");

    auto missing = CamEngineStatus::payloadMissing("/tmp/dw-cam-payload");
    EXPECT_FALSE(missing.ready);
    EXPECT_NE(missing.reason.find("/tmp/dw-cam-payload"), std::string::npos);

    auto unavailable = CamEngineStatus::engineUnavailable();
    EXPECT_FALSE(unavailable.ready);
    EXPECT_FALSE(unavailable.reason.empty());

    auto unmanaged = CamEngineStatus::unmanagedPlatform();
    EXPECT_FALSE(unmanaged.ready);
    EXPECT_FALSE(unmanaged.reason.empty());
    EXPECT_NE(unmanaged.reason, unavailable.reason);
}

TEST(CamEngineRuntime, PayloadDirPrefersEnvironmentOverride) {
    TempDir dev;
    const char* previous = std::getenv("DW_CAM_ENGINE_DIR");
    const std::string saved = previous ? previous : "";

    test::setEnv("DW_CAM_ENGINE_DIR", dev.path().string().c_str());
    EXPECT_EQ(locatePayloadDir("/opt/dw/bin"), dev.path());

    test::unsetEnv("DW_CAM_ENGINE_DIR");
    EXPECT_FALSE(locatePayloadDir("/opt/dw/bin").empty());

    if (previous)
        test::setEnv("DW_CAM_ENGINE_DIR", saved.c_str());
}

} // namespace dw::cam
