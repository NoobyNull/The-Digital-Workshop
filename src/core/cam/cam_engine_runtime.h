#pragma once

#include <cstdint>
#include <string>

#include "../types.h"

namespace dw::cam {

struct CamEngineConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 8973; // bridge default; override via bridgePortFromEnv()
    Path payloadDir;
    bool manageProcess = true;
};

struct CamEngineStatus {
    bool ready = false;
    std::string reason;
    std::string endpoint;

    static CamEngineStatus ok(std::string endpoint);
    static CamEngineStatus payloadMissing(const Path& dir);
    static CamEngineStatus engineUnavailable();
    static CamEngineStatus unmanagedPlatform();
};

[[nodiscard]] std::string baseUrl(const CamEngineConfig& config);
[[nodiscard]] bool payloadLooksComplete(const Path& dir);
// DW_CAM_ENGINE_DIR env override first (dev), then the bundled resource dir.
[[nodiscard]] Path locatePayloadDir(const Path& exeDir);
// DW_BRIDGE_PORT env override (dev), else fallback. Invalid values fall back.
[[nodiscard]] uint16_t bridgePortFromEnv(uint16_t fallback);

class CamEngineRuntime {
  public:
    explicit CamEngineRuntime(CamEngineConfig config = {});
    ~CamEngineRuntime();

    CamEngineRuntime(const CamEngineRuntime&) = delete;
    CamEngineRuntime& operator=(const CamEngineRuntime&) = delete;

    CamEngineStatus ensureReady();
    void stopOwnedProcess();
    [[nodiscard]] bool ownsProcess() const;
    // Cheap liveness check for status surfaces: true (and reaps) when the
    // owned child has exited since the last call. Never blocks.
    [[nodiscard]] bool ownedChildExited();
    [[nodiscard]] const CamEngineConfig& config() const { return m_config; }

  private:
    bool startOwnedProcess();
    bool waitUntilReachable();
    [[nodiscard]] bool isReachable() const;

    CamEngineConfig m_config;
    int m_pid = -1;
};

} // namespace dw::cam
