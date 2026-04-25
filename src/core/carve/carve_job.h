#pragma once

#include "../cnc/cnc_tool.h"
#include "../mesh/vertex.h"
#include "../types.h"
#include "heightmap.h"
#include "island_detector.h"
#include "model_fitter.h"
#include "surface_analysis.h"
#include "carve_streamer.h"
#include "toolpath_types.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace dw {
namespace carve {

enum class CarveJobState {
    Idle,
    Computing,
    Ready,
    Error
};

class CarveJob {
public:
    CarveJob() = default;
    ~CarveJob();

    // Non-copyable
    CarveJob(const CarveJob&) = delete;
    CarveJob& operator=(const CarveJob&) = delete;

    // Start heightmap generation (non-blocking)
    void startHeightmap(const std::vector<Vertex>& vertices,
                        const std::vector<u32>& indices,
                        const ModelFitter& fitter,
                        const FitParams& fitParams,
                        const HeightmapConfig& hmConfig);

    // Poll state (call from main thread)
    CarveJobState state() const;
    f32 progress() const;           // [0.0, 1.0]
    const Heightmap& heightmap() const;
    std::string errorMessage() const;

    // Cancel in-progress computation
    void cancel();

    // Force state to Ready (used when loading a saved heightmap)
    void setReady();

    // Load a previously saved heightmap from disk
    bool loadHeightmap(const std::string& path);

    // Analysis results (available after heightmap)
    const CurvatureResult& curvatureResult() const;
    const IslandResult& islandResult() const;

    // Run analysis after heightmap (call from main thread, fast)
    void analyzeHeightmap(f32 toolAngleDeg);

    // Toolpath generation
    void generateToolpath(const ToolpathConfig& config,
                          const VtdbToolGeometry& finishTool,
                          const VtdbToolGeometry* clearTool);
    const MultiPassToolpath& toolpath() const;

    // Start streaming the generated toolpath
    void startStreaming(CncController* controller);

    // Streaming state accessors
    CarveStreamer* streamer();

private:
    std::atomic<CarveJobState> m_state{CarveJobState::Idle};
    std::atomic<f32> m_progress{0.0f};
    std::atomic<bool> m_cancelled{false};
    Heightmap m_heightmap;
    CurvatureResult m_curvature;
    IslandResult m_islands;
    MultiPassToolpath m_toolpath;
    ToolpathConfig m_toolpathConfig;
    bool m_analyzed = false;
    std::string m_error;
    std::future<void> m_future;
    std::unique_ptr<CarveStreamer> m_streamer;
};

} // namespace carve
} // namespace dw
