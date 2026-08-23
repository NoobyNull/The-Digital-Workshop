// Digital Workshop - CarveJob Tests

#include <gtest/gtest.h>

#include "core/carve/carve_job.h"
#include "core/cnc/cnc_controller.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <thread>
#include <utility>

using namespace dw;
using namespace dw::carve;

namespace {

// Helper: create a flat quad mesh
void makeFlatMesh(f32 size, f32 z, std::vector<Vertex>& verts, std::vector<u32>& indices) {
    verts.clear();
    indices.clear();
    verts.push_back(Vertex{Vec3{0.0f, 0.0f, z}});
    verts.push_back(Vertex{Vec3{size, 0.0f, z}});
    verts.push_back(Vertex{Vec3{size, size, z}});
    verts.push_back(Vertex{Vec3{0.0f, size, z}});
    indices = {0, 1, 2, 0, 2, 3};
}

void makeSurfaceMesh(f32 size,
                     f32 resolution,
                     const std::function<f32(f32, f32)>& heightAt,
                     std::vector<Vertex>& verts,
                     std::vector<u32>& indices,
                     f32& minZ,
                     f32& maxZ) {
    const int gridSize = static_cast<int>(size / resolution) + 1;
    verts.clear();
    indices.clear();
    verts.reserve(static_cast<usize>(gridSize * gridSize));
    minZ = std::numeric_limits<f32>::max();
    maxZ = std::numeric_limits<f32>::lowest();

    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            const f32 x = static_cast<f32>(col) * resolution;
            const f32 y = static_cast<f32>(row) * resolution;
            const f32 z = heightAt(x, y);
            verts.push_back(Vertex{Vec3{x, y, z}});
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    }

    for (int row = 0; row < gridSize - 1; ++row) {
        for (int col = 0; col < gridSize - 1; ++col) {
            const auto first = static_cast<u32>(row * gridSize + col);
            const auto nextRow = first + static_cast<u32>(gridSize);
            indices.insert(indices.end(),
                           {first, first + 1, nextRow, first + 1, nextRow + 1, nextRow});
        }
    }
}

bool prepareSurfaceJob(CarveJob& job, bool withIsland) {
    constexpr f32 kSize = 10.0f;
    constexpr f32 kResolution = 1.0f;
    std::vector<Vertex> verts;
    std::vector<u32> indices;
    f32 minZ = 0.0f;
    f32 maxZ = 0.0f;
    makeSurfaceMesh(
        kSize,
        kResolution,
        [withIsland](f32 x, f32 y) {
            if (withIsland) {
                // Give the controlled island mask below a representative
                // steep-pocket surface to generate against.
                return (std::abs(x - 5.0f) < 0.1f && std::abs(y - 5.0f) < 0.1f) ? 0.0f : 10.0f;
            }
            // A gentle open slope has no buried cells/islands.
            return x * 0.1f;
        },
        verts,
        indices,
        minZ,
        maxZ);

    ModelFitter fitter;
    fitter.setModelBounds(Vec3{0.0f, 0.0f, minZ}, Vec3{kSize, kSize, maxZ});
    StockDimensions stock;
    stock.width = kSize;
    stock.height = kSize;
    stock.thickness = std::max(1.0f, maxZ - minZ);
    fitter.setStock(stock);

    FitParams fit;
    fit.scale = 1.0f;
    fit.depthMm = maxZ - minZ;

    HeightmapConfig heightmap;
    heightmap.resolutionMm = kResolution;
    job.startHeightmap(verts, indices, fitter, fit, heightmap);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (job.state() == CarveJobState::Computing) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return job.state() == CarveJobState::Ready;
}

VtdbToolGeometry makeSurfaceTool(const std::string& id,
                                 const std::string& name,
                                 VtdbToolType type,
                                 VtdbUnits units,
                                 f64 diameter) {
    VtdbToolGeometry tool;
    tool.id = id;
    tool.name_format = name;
    tool.tool_type = type;
    tool.units = units;
    tool.diameter = diameter;
    tool.included_angle = 30.0;
    tool.num_flutes = 2;
    return tool;
}

ToolpathConfig surfaceToolpathConfig() {
    ToolpathConfig config;
    config.axis = ScanAxis::XOnly;
    config.direction = MillDirection::Alternating;
    config.customStepoverPct = 100.0f;
    config.scanResolutionMm = 1.0f;
    config.safeZMm = 5.0f;
    return config;
}

void installSingleSurfaceIsland(CarveJob& job) {
    // Island detection has its own focused test suite. These CarveJob tests
    // control that upstream result so they isolate multi-tool generation and
    // can compare its unit and metadata contracts deterministically.
    auto& islands = const_cast<IslandResult&>(job.islandResult());
    islands = IslandResult{};
    islands.maskCols = job.heightmap().cols();
    islands.maskRows = job.heightmap().rows();
    islands.islandMask.assign(static_cast<usize>(islands.maskCols * islands.maskRows), -1);

    const int col = islands.maskCols / 2;
    const int row = islands.maskRows / 2;
    const f32 x = job.heightmap().boundsMin().x +
                  static_cast<f32>(col) * job.heightmap().resolution();
    const f32 y = job.heightmap().boundsMin().y +
                  static_cast<f32>(row) * job.heightmap().resolution();

    Island island;
    island.id = 0;
    island.cells.push_back({col, row});
    island.minZ = -5.0f;
    island.maxZ = 0.0f;
    island.depth = 5.0f;
    island.areaMm2 = job.heightmap().resolution() * job.heightmap().resolution();
    island.minClearDiameter = 2.0f;
    island.centroid = Vec2{x, y};
    island.boundsMin = Vec2{x, y};
    island.boundsMax = Vec2{x, y};
    islands.islandMask[static_cast<usize>(row * islands.maskCols + col)] = 0;
    islands.islands.push_back(std::move(island));
}

} // namespace

TEST(CarveJob, InitialState) {
    CarveJob job;
    EXPECT_EQ(job.state(), CarveJobState::Idle);
    EXPECT_FLOAT_EQ(job.progress(), 0.0f);
    EXPECT_TRUE(job.heightmap().empty());
}

TEST(CarveJob, ComputeSimpleMesh) {
    CarveJob job;

    std::vector<Vertex> verts;
    std::vector<u32> indices;
    makeFlatMesh(10.0f, 5.0f, verts, indices);

    ModelFitter fitter;
    fitter.setModelBounds(Vec3{0, 0, 0}, Vec3{10, 10, 5});

    StockDimensions stock;
    stock.width = 10.0f;
    stock.height = 10.0f;
    stock.thickness = 5.0f;
    fitter.setStock(stock);

    FitParams fp;
    fp.scale = 1.0f;

    HeightmapConfig hcfg;
    hcfg.resolutionMm = 1.0f;

    job.startHeightmap(verts, indices, fitter, fp, hcfg);

    // Wait for completion (with timeout)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (job.state() == CarveJobState::Computing) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL() << "CarveJob timed out";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(job.state(), CarveJobState::Ready);
    EXPECT_FALSE(job.heightmap().empty());
    EXPECT_TRUE(job.errorMessage().empty());
}

TEST(CarveJob, HeightmapToolpathUsesWorkZeroAtMaterialTop) {
    CarveJob job;

    std::vector<Vertex> verts;
    std::vector<u32> indices;
    makeFlatMesh(10.0f, 5.0f, verts, indices);

    ModelFitter fitter;
    fitter.setModelBounds(Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 10.0f, 5.0f});

    StockDimensions stock;
    stock.width = 40.0f;
    stock.height = 40.0f;
    stock.thickness = 19.0f;
    fitter.setStock(stock);

    FitParams fp;
    fp.scale = 1.0f;
    fp.depthMm = 7.0f;

    HeightmapConfig hcfg;
    hcfg.resolutionMm = 1.0f;

    job.startHeightmap(verts, indices, fitter, fp, hcfg);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (job.state() == CarveJobState::Computing) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL() << "CarveJob timed out";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_EQ(job.state(), CarveJobState::Ready);
    ASSERT_FALSE(job.heightmap().empty());
    EXPECT_NEAR(job.heightmap().boundsMax().z, 0.0f, 0.001f);
    EXPECT_NEAR(job.heightmap().boundsMin().z, -7.0f, 0.001f);
    EXPECT_NEAR(job.heightmap().atMm(5.0f, 5.0f), 0.0f, 0.001f);

    VtdbToolGeometry finishTool;
    finishTool.tool_type = VtdbToolType::VBit;
    finishTool.units = VtdbUnits::Metric;
    finishTool.diameter = 3.175;
    finishTool.included_angle = 90.0;

    job.analyzeHeightmap(static_cast<f32>(finishTool.included_angle));

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 100.0f;
    cfg.scanResolutionMm = 5.0f;
    cfg.safeZMm = 5.0f;

    job.generateToolpath(cfg, finishTool, nullptr);

    const auto& finishing = job.toolpath().finishing;
    ASSERT_FALSE(finishing.points.empty());
    for (const auto& pt : finishing.points) {
        if (pt.rapid) {
            EXPECT_NEAR(pt.position.z, cfg.safeZMm, 0.001f);
        } else {
            EXPECT_LE(pt.position.z, 0.001f);
        }
    }
}

TEST(CarveJob, SurfaceClearingUsesEquivalentMetricAndImperialDiameters) {
    CarveJob job;
    ASSERT_TRUE(prepareSurfaceJob(job, true));

    const auto finish =
        makeSurfaceTool("finish", "Fine V-bit", VtdbToolType::VBit, VtdbUnits::Metric, 1.0);
    const auto metricClearing = makeSurfaceTool(
        "metric-clear", "2mm clearing tool", VtdbToolType::EndMill, VtdbUnits::Metric, 2.0);
    const auto imperialClearing = makeSurfaceTool("imperial-clear",
                                                  "2mm equivalent clearing tool",
                                                  VtdbToolType::EndMill,
                                                  VtdbUnits::Imperial,
                                                  2.0 / 25.4);

    job.analyzeHeightmap(static_cast<f32>(finish.included_angle));
    installSingleSurfaceIsland(job);

    const ToolpathConfig config = surfaceToolpathConfig();
    job.generateToolpath(config, finish, &metricClearing);
    const Toolpath metricPath = job.toolpath().clearing;
    ASSERT_FALSE(metricPath.points.empty());

    job.generateToolpath(config, finish, &imperialClearing);
    const Toolpath& imperialPath = job.toolpath().clearing;
    ASSERT_FALSE(imperialPath.points.empty());

    EXPECT_EQ(imperialPath.points.size(), metricPath.points.size());
    EXPECT_EQ(imperialPath.lineCount, metricPath.lineCount);
    EXPECT_EQ(imperialPath.scanLineCount, metricPath.scanLineCount);
    EXPECT_NEAR(imperialPath.totalDistanceMm, metricPath.totalDistanceMm, 0.001f);
    EXPECT_NEAR(imperialPath.estimatedTimeSec, metricPath.estimatedTimeSec, 0.001f);
}

TEST(CarveJob, SurfaceDistinctClearingToolAddsPauseMetadataAndBoundaryLines) {
    CarveJob job;
    ASSERT_TRUE(prepareSurfaceJob(job, true));

    const auto finish =
        makeSurfaceTool("finish", "Fine V-bit", VtdbToolType::VBit, VtdbUnits::Metric, 1.0);
    const auto clearing = makeSurfaceTool(
        "clear", "Clearing End Mill", VtdbToolType::EndMill, VtdbUnits::Metric, 2.0);

    job.analyzeHeightmap(static_cast<f32>(finish.included_angle));
    installSingleSurfaceIsland(job);
    job.generateToolpath(surfaceToolpathConfig(), finish, &clearing);

    const MultiPassToolpath& path = job.toolpath();
    ASSERT_FALSE(path.clearing.points.empty());
    ASSERT_FALSE(path.finishing.points.empty());
    EXPECT_TRUE(path.requiresToolChange);
    EXPECT_EQ(path.clearingToolName, "Clearing End Mill");
    EXPECT_EQ(path.finishingToolName, "Fine V-bit");
    EXPECT_EQ(path.totalLineCount, path.clearing.lineCount + path.finishing.lineCount + 6);
}

TEST(CarveJob, SurfaceSameToolClearingDoesNotAddPause) {
    CarveJob job;
    ASSERT_TRUE(prepareSurfaceJob(job, true));

    const auto tool =
        makeSurfaceTool("shared-tool", "Shared V-bit", VtdbToolType::VBit, VtdbUnits::Metric, 2.0);

    job.analyzeHeightmap(static_cast<f32>(tool.included_angle));
    installSingleSurfaceIsland(job);
    job.generateToolpath(surfaceToolpathConfig(), tool, &tool);

    const MultiPassToolpath& path = job.toolpath();
    ASSERT_FALSE(path.clearing.points.empty());
    EXPECT_FALSE(path.requiresToolChange);
    EXPECT_EQ(path.clearingToolName, "Shared V-bit");
    EXPECT_EQ(path.finishingToolName, "Shared V-bit");
    EXPECT_EQ(path.totalLineCount, path.clearing.lineCount + path.finishing.lineCount);
}

TEST(CarveJob, SurfaceNoIslandsClearsEarlierClearingMetadata) {
    CarveJob job;
    ASSERT_TRUE(prepareSurfaceJob(job, true));

    const auto firstFinish = makeSurfaceTool(
        "first-finish", "First finishing tool", VtdbToolType::VBit, VtdbUnits::Metric, 1.0);
    const auto clearing = makeSurfaceTool(
        "clear", "Earlier clearing tool", VtdbToolType::EndMill, VtdbUnits::Metric, 2.0);

    job.analyzeHeightmap(static_cast<f32>(firstFinish.included_angle));
    installSingleSurfaceIsland(job);
    job.generateToolpath(surfaceToolpathConfig(), firstFinish, &clearing);
    ASSERT_TRUE(job.toolpath().requiresToolChange);
    ASSERT_FALSE(job.toolpath().clearingToolName.empty());

    ASSERT_TRUE(prepareSurfaceJob(job, false));
    const auto nextFinish = makeSurfaceTool(
        "next-finish", "Next finishing tool", VtdbToolType::VBit, VtdbUnits::Metric, 1.0);
    job.analyzeHeightmap(static_cast<f32>(nextFinish.included_angle));
    ASSERT_TRUE(job.islandResult().islands.empty());
    job.generateToolpath(surfaceToolpathConfig(), nextFinish, &clearing);

    const MultiPassToolpath& path = job.toolpath();
    EXPECT_TRUE(path.clearing.points.empty());
    EXPECT_FALSE(path.requiresToolChange);
    EXPECT_TRUE(path.clearingToolName.empty());
    EXPECT_EQ(path.finishingToolName, "Next finishing tool");
    EXPECT_EQ(path.totalLineCount, path.finishing.lineCount);
    EXPECT_FLOAT_EQ(path.totalTimeSec, path.finishing.estimatedTimeSec);
}

TEST(CarveJob, CancelMidCompute) {
    CarveJob job;

    // Create a mesh with enough resolution to take some time
    std::vector<Vertex> verts;
    std::vector<u32> indices;
    makeFlatMesh(100.0f, 5.0f, verts, indices);

    ModelFitter fitter;
    fitter.setModelBounds(Vec3{0, 0, 0}, Vec3{100, 100, 5});

    StockDimensions stock;
    stock.width = 100.0f;
    stock.height = 100.0f;
    stock.thickness = 5.0f;
    fitter.setStock(stock);

    FitParams fp;
    fp.scale = 1.0f;

    HeightmapConfig hcfg;
    hcfg.resolutionMm = 0.01f; // Very fine grid to ensure it takes time

    job.startHeightmap(verts, indices, fitter, fp, hcfg);

    // Cancel immediately
    job.cancel();

    // Wait for the job to finish (cancelled)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (job.state() == CarveJobState::Computing) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL() << "CarveJob cancel timed out";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Should be back to Idle (cancelled)
    EXPECT_EQ(job.state(), CarveJobState::Idle);
}

TEST(CarveJob, FixedDepthToolpathIncludesAutomaticClearingOutsideModel) {
    CarveJob job;

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Alternating;
    cfg.customStepoverPct = 100.0f;
    cfg.scanResolutionMm = 1.0f;
    cfg.stepdownMm = 10.0f;

    VtdbToolGeometry finishTool;
    finishTool.id = "finish";
    finishTool.tool_type = VtdbToolType::TaperedBallNose;
    finishTool.units = VtdbUnits::Metric;
    finishTool.diameter = 1.0;

    VtdbToolGeometry roughingTool;
    roughingTool.id = "rough";
    roughingTool.tool_type = VtdbToolType::EndMill;
    roughingTool.units = VtdbUnits::Metric;
    roughingTool.diameter = 4.0;

    job.generateFixedDepthToolpath(Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{30.0f, 20.0f, 0.0f},
                                   Vec3{10.0f, 5.0f, 0.0f},
                                   Vec3{20.0f, 15.0f, 0.0f},
                                   2.0f,
                                   cfg,
                                   finishTool,
                                   &roughingTool);

    ASSERT_EQ(job.state(), CarveJobState::Ready);
    const auto& path = job.toolpath();
    EXPECT_FALSE(path.clearing.points.empty());
    EXPECT_FALSE(path.finishing.points.empty());
    EXPECT_TRUE(path.requiresToolChange);
    EXPECT_GT(path.totalLineCount, path.finishing.lineCount);
}

TEST(CarveJob, FixedDepthEmptyClearingDoesNotAdvertisePhantomTool) {
    CarveJob job;

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Alternating;
    cfg.cutExtents = CutExtents::Model;
    cfg.customStepoverPct = 100.0f;
    cfg.scanResolutionMm = 1.0f;
    cfg.stepdownMm = 10.0f;

    auto finishTool =
        makeSurfaceTool("finish", "Finishing tool", VtdbToolType::BallNose, VtdbUnits::Metric, 1.0);
    auto clearingTool = makeSurfaceTool(
        "clear", "Unused clearing tool", VtdbToolType::EndMill, VtdbUnits::Metric, 6.0);

    job.generateFixedDepthToolpath(Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{30.0f, 20.0f, 0.0f},
                                   Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{30.0f, 20.0f, 0.0f},
                                   2.0f,
                                   cfg,
                                   finishTool,
                                   &clearingTool);

    ASSERT_EQ(job.state(), CarveJobState::Ready);
    const auto& path = job.toolpath();
    EXPECT_TRUE(path.clearing.points.empty());
    EXPECT_TRUE(path.clearingToolName.empty());
    EXPECT_FALSE(path.requiresToolChange);
    EXPECT_EQ(path.totalLineCount, path.finishing.lineCount);
}

TEST(CarveJob, FixedDepthToolpathCanRasterFullMaterialExtents) {
    CarveJob job;

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.cutExtents = CutExtents::Material;
    cfg.customStepoverPct = 100.0f;
    cfg.scanResolutionMm = 5.0f;
    cfg.stepdownMm = 10.0f;

    VtdbToolGeometry finishTool;
    finishTool.id = "finish";
    finishTool.tool_type = VtdbToolType::EndMill;
    finishTool.units = VtdbUnits::Metric;
    finishTool.diameter = 5.0;

    job.generateFixedDepthToolpath(Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{30.0f, 20.0f, 0.0f},
                                   Vec3{10.0f, 5.0f, 0.0f},
                                   Vec3{20.0f, 15.0f, 0.0f},
                                   2.0f,
                                   cfg,
                                   finishTool);

    ASSERT_EQ(job.state(), CarveJobState::Ready);
    const auto& finishing = job.toolpath().finishing;
    ASSERT_FALSE(finishing.points.empty());

    f32 maxCutX = -1.0f;
    f32 maxCutY = -1.0f;
    for (const auto& pt : finishing.points) {
        if (pt.rapid)
            continue;
        maxCutX = std::max(maxCutX, pt.position.x);
        maxCutY = std::max(maxCutY, pt.position.y);
    }

    EXPECT_NEAR(maxCutX, 30.0f, 0.001f);
    EXPECT_NEAR(maxCutY, 20.0f, 0.001f);
}

TEST(CarveJob, StartStreamingSubmitsGeneratedProgramToConnectedController) {
    CarveJob job;
    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Alternating;
    cfg.customStepoverPct = 100.0f;
    cfg.scanResolutionMm = 5.0f;
    cfg.stepdownMm = 10.0f;

    VtdbToolGeometry tool;
    tool.id = "finish";
    tool.tool_type = VtdbToolType::EndMill;
    tool.units = VtdbUnits::Metric;
    tool.diameter = 5.0;

    job.generateFixedDepthToolpath(Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{30.0f, 20.0f, 0.0f},
                                   Vec3{5.0f, 5.0f, 0.0f},
                                   Vec3{25.0f, 15.0f, 0.0f},
                                   2.0f,
                                   cfg,
                                   tool);
    ASSERT_EQ(job.state(), CarveJobState::Ready);

    CncController disconnected(nullptr);
    EXPECT_FALSE(job.startStreaming(&disconnected));

    CncController controller(nullptr);
    ASSERT_TRUE(controller.connectSimulator());
    for (int attempts = 0; attempts < 100 && !controller.isConnected(); ++attempts) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(controller.isConnected());
    ASSERT_TRUE(job.startStreaming(&controller));
    EXPECT_GT(controller.streamProgress().totalLines, 0);
    controller.stopStream();
    controller.disconnect();
}
