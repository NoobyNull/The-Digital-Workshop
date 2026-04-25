#include "carve_job.h"
#include "surface_analysis.h"
#include "island_detector.h"
#include "toolpath_generator.h"
#include "../cnc/cnc_controller.h"

#include <stdexcept>

namespace dw {
namespace carve {

CarveJob::~CarveJob()
{
    cancel();
    if (m_future.valid()) {
        m_future.wait();
    }
}

void CarveJob::startHeightmap(const std::vector<Vertex>& vertices,
                               const std::vector<u32>& indices,
                               const ModelFitter& fitter,
                               const FitParams& fitParams,
                               const HeightmapConfig& hmConfig)
{
    // Wait for any previous job to finish
    if (m_future.valid()) {
        m_future.wait();
    }

    m_state.store(CarveJobState::Computing, std::memory_order_release);
    m_progress.store(0.0f, std::memory_order_release);
    m_cancelled.store(false, std::memory_order_release);
    m_error.clear();

    // Transform vertices using ModelFitter
    std::vector<Vertex> transformed;
    transformed.reserve(vertices.size());
    for (const auto& v : vertices) {
        Vertex tv = v;
        tv.position = fitter.transform(v.position, fitParams);
        transformed.push_back(tv);
    }

    // Compute transformed bounds
    FitResult fitResult = fitter.fit(fitParams);

    // Capture copies for the async lambda
    auto capturedVerts = std::move(transformed);
    auto capturedIndices = indices;
    auto capturedConfig = hmConfig;
    Vec3 boundsMin = fitResult.modelMin;
    Vec3 boundsMax = fitResult.modelMax;

    m_future = std::async(std::launch::async,
        [this, verts = std::move(capturedVerts),
         idxs = std::move(capturedIndices),
         cfg = capturedConfig,
         bMin = boundsMin, bMax = boundsMax]() {

        try {
            m_heightmap.build(verts, idxs, bMin, bMax, cfg,
                [this](f32 p) {
                    m_progress.store(p, std::memory_order_release);
                    if (m_cancelled.load(std::memory_order_acquire)) {
                        throw std::runtime_error("Cancelled");
                    }
                });

            if (m_cancelled.load(std::memory_order_acquire)) {
                m_state.store(CarveJobState::Idle, std::memory_order_release);
            } else {
                m_state.store(CarveJobState::Ready, std::memory_order_release);
            }
        } catch (const std::exception& e) {
            if (m_cancelled.load(std::memory_order_acquire)) {
                m_state.store(CarveJobState::Idle, std::memory_order_release);
            } else {
                m_error = e.what();
                m_state.store(CarveJobState::Error, std::memory_order_release);
            }
        }
    });
}

CarveJobState CarveJob::state() const
{
    return m_state.load(std::memory_order_acquire);
}

f32 CarveJob::progress() const
{
    return m_progress.load(std::memory_order_acquire);
}

const Heightmap& CarveJob::heightmap() const
{
    return m_heightmap;
}

std::string CarveJob::errorMessage() const
{
    return m_error;
}

void CarveJob::cancel()
{
    m_cancelled.store(true, std::memory_order_release);
}

void CarveJob::setReady()
{
    m_state.store(CarveJobState::Ready, std::memory_order_release);
    m_progress.store(1.0f, std::memory_order_release);
}

bool CarveJob::loadHeightmap(const std::string& path)
{
    if (!m_heightmap.load(path)) return false;
    setReady();
    return true;
}

const CurvatureResult& CarveJob::curvatureResult() const
{
    return m_curvature;
}

const IslandResult& CarveJob::islandResult() const
{
    return m_islands;
}

void CarveJob::analyzeHeightmap(f32 toolAngleDeg)
{
    if (m_state.load(std::memory_order_acquire) != CarveJobState::Ready) {
        return;
    }
    m_curvature = analyzeCurvature(m_heightmap);
    m_islands = detectIslands(m_heightmap, toolAngleDeg);
    m_analyzed = true;
}

void CarveJob::generateToolpath(const ToolpathConfig& config,
                                 const VtdbToolGeometry& finishTool,
                                 const VtdbToolGeometry* clearTool)
{
    if (!m_analyzed) {
        return;
    }

    m_toolpathConfig = config;
    ToolpathGenerator gen;

    // Use flat_diameter when available (end mills); fall back to full diameter
    // for V-bits and ball noses where flat_diameter is 0
    const f32 tipDia = (finishTool.flat_diameter > 0.0)
        ? static_cast<f32>(finishTool.flat_diameter)
        : static_cast<f32>(finishTool.diameter);

    m_toolpath.finishing = gen.generateFinishing(
        m_heightmap, config, tipDia, finishTool);

    if (clearTool && !m_islands.islands.empty()) {
        m_toolpath.clearing = gen.generateClearing(
            m_heightmap, m_islands, config,
            static_cast<f32>(clearTool->diameter));
        m_toolpath.totalTimeSec =
            m_toolpath.finishing.estimatedTimeSec +
            m_toolpath.clearing.estimatedTimeSec;
        m_toolpath.totalLineCount =
            m_toolpath.finishing.lineCount +
            m_toolpath.clearing.lineCount;
    } else {
        m_toolpath.clearing = Toolpath{};
        m_toolpath.totalTimeSec = m_toolpath.finishing.estimatedTimeSec;
        m_toolpath.totalLineCount = m_toolpath.finishing.lineCount;
    }
}

const MultiPassToolpath& CarveJob::toolpath() const
{
    return m_toolpath;
}

void CarveJob::startStreaming(CncController* controller)
{
    if (!m_analyzed || m_toolpath.finishing.points.empty()) {
        return;
    }

    m_streamer = std::make_unique<CarveStreamer>();
    m_streamer->setCncController(controller);
    const auto units = controller ? controller->sendUnits() : cnc::SendUnits::Millimeters;
    m_streamer->start(m_toolpath, m_toolpathConfig, units);
}

CarveStreamer* CarveJob::streamer()
{
    return m_streamer.get();
}

} // namespace carve
} // namespace dw
