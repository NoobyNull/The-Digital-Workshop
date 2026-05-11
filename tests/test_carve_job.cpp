// Digital Workshop - CarveJob Tests

#include <gtest/gtest.h>

#include "core/carve/carve_job.h"

#include <chrono>
#include <thread>

using namespace dw;
using namespace dw::carve;

namespace {

// Helper: create a flat quad mesh
void makeFlatMesh(f32 size, f32 z,
                  std::vector<Vertex>& verts,
                  std::vector<u32>& indices)
{
    verts.clear();
    indices.clear();
    verts.push_back(Vertex{Vec3{0.0f, 0.0f, z}});
    verts.push_back(Vertex{Vec3{size, 0.0f, z}});
    verts.push_back(Vertex{Vec3{size, size, z}});
    verts.push_back(Vertex{Vec3{0.0f, size, z}});
    indices = {0, 1, 2, 0, 2, 3};
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

TEST(CarveJob, FixedDepthToolpathIncludesAutomaticClearingOutsideModel)
{
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

    job.generateFixedDepthToolpath(
        Vec3{0.0f, 0.0f, 0.0f},
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

TEST(CarveJob, FixedDepthToolpathCanRasterFullMaterialExtents)
{
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

    job.generateFixedDepthToolpath(
        Vec3{0.0f, 0.0f, 0.0f},
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
        if (pt.rapid) continue;
        maxCutX = std::max(maxCutX, pt.position.x);
        maxCutY = std::max(maxCutY, pt.position.y);
    }

    EXPECT_NEAR(maxCutX, 30.0f, 0.001f);
    EXPECT_NEAR(maxCutY, 20.0f, 0.001f);
}
