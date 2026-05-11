#include "carve_job.h"
#include "surface_analysis.h"
#include "island_detector.h"
#include "toolpath_generator.h"
#include "../cnc/cnc_controller.h"

#include <cmath>
#include <stdexcept>

namespace dw {
namespace carve {
namespace {

f64 diameterMm(const VtdbToolGeometry& tool)
{
    f64 diameter = tool.diameter > 0.0 ? tool.diameter : tool.flat_diameter;
    if (tool.units == VtdbUnits::Imperial) {
        diameter *= 25.4;
    }
    return diameter;
}

f64 tipDiameterMm(const VtdbToolGeometry& tool)
{
    f64 diameter = 0.0;
    if (tool.flat_diameter > 0.0) {
        diameter = tool.flat_diameter;
    } else if (tool.tip_radius > 0.0) {
        diameter = tool.tip_radius * 2.0;
    } else {
        diameter = tool.diameter;
    }
    if (tool.units == VtdbUnits::Imperial) {
        diameter *= 25.4;
    }
    return diameter;
}

bool sameTool(const VtdbToolGeometry& a, const VtdbToolGeometry& b)
{
    if (!a.id.empty() && !b.id.empty()) {
        return a.id == b.id;
    }

    return a.tool_type == b.tool_type &&
           a.units == b.units &&
           std::abs(a.diameter - b.diameter) < 0.0001 &&
           std::abs(a.flat_diameter - b.flat_diameter) < 0.0001 &&
           std::abs(a.tip_radius - b.tip_radius) < 0.0001;
}

} // namespace

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

    // Transform vertices using ModelFitter. ModelFitter works in stock space
    // where material top is stock.thickness; G-code uses WCS space where
    // material top is Z0 and cuts go negative. Normalize here so every
    // downstream heightmap/toolpath/export path uses the machine work frame.
    FitResult fitResult = fitter.fit(fitParams);
    const f32 zOrigin = fitResult.modelMax.z;
    std::vector<Vertex> transformed;
    transformed.reserve(vertices.size());
    for (const auto& v : vertices) {
        Vertex tv = v;
        tv.position = fitter.transform(v.position, fitParams);
        tv.position.z -= zOrigin;
        transformed.push_back(tv);
    }

    // Compute transformed bounds
    fitResult.modelMin.z -= zOrigin;
    fitResult.modelMax.z -= zOrigin;

    HeightmapConfig normalizedConfig = hmConfig;
    normalizedConfig.defaultZ -= zOrigin;

    // Capture copies for the async lambda
    auto capturedVerts = std::move(transformed);
    auto capturedIndices = indices;
    auto capturedConfig = normalizedConfig;
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

    const f32 tipDia = static_cast<f32>(tipDiameterMm(finishTool));

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

void CarveJob::generateFixedDepthToolpath(const Vec3& stockMin,
                                           const Vec3& stockMax,
                                           const Vec3& modelMin,
                                           const Vec3& modelMax,
                                           f32 depthMm,
                                           const ToolpathConfig& config,
                                           const VtdbToolGeometry& finishTool,
                                           const VtdbToolGeometry* roughingTool)
{
    if (m_future.valid()) {
        m_future.wait();
    }

    m_toolpathConfig = config;
    m_error.clear();
    m_cancelled.store(false, std::memory_order_release);

    ToolpathGenerator gen;
    const f64 finishDia = tipDiameterMm(finishTool);

    m_toolpath.clearing = Toolpath{};
    m_toolpath.requiresToolChange = false;
    m_toolpath.clearingToolName.clear();
    m_toolpath.finishingToolName = resolveToolNameFormat(finishTool);

    if (roughingTool) {
        const f64 roughDia = diameterMm(*roughingTool);
        if (config.cutExtents == CutExtents::Material) {
            m_toolpath.clearing = gen.generateFixedDepthRaster(
                stockMin, stockMax, config, static_cast<f32>(roughDia),
                depthMm);
        } else {
            m_toolpath.clearing = gen.generateFixedDepthClearingAroundModel(
                stockMin, stockMax, modelMin, modelMax, config,
                static_cast<f32>(roughDia), depthMm);
        }
        m_toolpath.clearingToolName = resolveToolNameFormat(*roughingTool);
        m_toolpath.requiresToolChange =
            !m_toolpath.clearing.points.empty() &&
            !sameTool(*roughingTool, finishTool);
    }
    const Vec3& finishMin =
        config.cutExtents == CutExtents::Material ? stockMin : modelMin;
    const Vec3& finishMax =
        config.cutExtents == CutExtents::Material ? stockMax : modelMax;
    m_toolpath.finishing = gen.generateFixedDepthRaster(
        finishMin, finishMax, config, static_cast<f32>(finishDia), depthMm);
    m_toolpath.totalTimeSec =
        m_toolpath.clearing.estimatedTimeSec +
        m_toolpath.finishing.estimatedTimeSec;
    m_toolpath.totalLineCount =
        m_toolpath.clearing.lineCount + m_toolpath.finishing.lineCount +
        (m_toolpath.requiresToolChange ? 6 : 0);
    m_curvature = CurvatureResult{};
    m_islands = IslandResult{};
    m_analyzed = true;
    m_progress.store(1.0f, std::memory_order_release);
    m_state.store(m_toolpath.finishing.points.empty()
                      ? CarveJobState::Error
                      : CarveJobState::Ready,
                  std::memory_order_release);
    if (m_toolpath.finishing.points.empty()) {
        m_error = "Fixed-depth raster toolpath is empty";
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
