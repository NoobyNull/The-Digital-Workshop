#include <gtest/gtest.h>

#include <algorithm>

#include "core/carve/toolpath_generator.h"
#include "core/cnc/cnc_tool.h"

using namespace dw;
using namespace dw::carve;

// Helper: create a V-bit tool geometry
static VtdbToolGeometry makeVBit(f64 diameter, f64 includedAngle)
{
    VtdbToolGeometry t;
    t.tool_type = VtdbToolType::VBit;
    t.diameter = diameter;
    t.included_angle = includedAngle;
    return t;
}

// Helper: create a ball nose tool geometry
static VtdbToolGeometry makeBallNose(f64 diameter)
{
    VtdbToolGeometry t;
    t.tool_type = VtdbToolType::BallNose;
    t.diameter = diameter;
    t.tip_radius = diameter * 0.5;
    return t;
}

// Helper: create an end mill tool geometry
static VtdbToolGeometry makeEndMill(f64 diameter)
{
    VtdbToolGeometry t;
    t.tool_type = VtdbToolType::EndMill;
    t.diameter = diameter;
    return t;
}

// Default tool for tests that don't care about offset
static VtdbToolGeometry defaultTool()
{
    return makeVBit(4.0, 90.0);
}

// Helper: build a flat heightmap at a given Z
static Heightmap makeFlatHeightmap(f32 z, f32 widthMm, f32 heightMm, f32 res)
{
    // Create a minimal mesh: two triangles forming a flat quad at the given Z
    std::vector<Vertex> verts(4);
    verts[0].position = {0.0f, 0.0f, z};
    verts[1].position = {widthMm, 0.0f, z};
    verts[2].position = {widthMm, heightMm, z};
    verts[3].position = {0.0f, heightMm, z};

    std::vector<u32> indices = {0, 1, 2, 0, 2, 3};

    Vec3 bmin = {0.0f, 0.0f, z};
    Vec3 bmax = {widthMm, heightMm, z};

    HeightmapConfig cfg;
    cfg.resolutionMm = res;
    cfg.defaultZ = 0.0f;

    Heightmap hm;
    hm.build(verts, indices, bmin, bmax, cfg);
    return hm;
}

// ---------------------------------------------------------------------------
// StepoverPresets
// ---------------------------------------------------------------------------

TEST(ToolpathGen, StepoverPresets)
{
    EXPECT_FLOAT_EQ(stepoverPercent(StepoverPreset::UltraFine), 1.0f);
    EXPECT_FLOAT_EQ(stepoverPercent(StepoverPreset::Fine), 8.0f);
    EXPECT_FLOAT_EQ(stepoverPercent(StepoverPreset::Basic), 12.0f);
    EXPECT_FLOAT_EQ(stepoverPercent(StepoverPreset::Rough), 25.0f);
    EXPECT_FLOAT_EQ(stepoverPercent(StepoverPreset::Roughing), 40.0f);
}

// ---------------------------------------------------------------------------
// FlatSurfaceXScan
// ---------------------------------------------------------------------------

TEST(ToolpathGen, FlatSurfaceXScan)
{
    // 10mm x 10mm flat surface at Z=-5
    Heightmap hm = makeFlatHeightmap(-5.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Alternating;
    cfg.stepoverPreset = StepoverPreset::Rough;  // 25% of 4mm = 1mm stepover

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    ASSERT_FALSE(path.points.empty());
    EXPECT_GT(path.totalDistanceMm, 0.0f);
    EXPECT_GT(path.estimatedTimeSec, 0.0f);

    // All cut points should be at Z=-5 (flat surface)
    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            EXPECT_NEAR(pt.position.z, -5.0f, 0.1f)
                << "Cut point Z should match flat surface height";
        }
    }
}

// ---------------------------------------------------------------------------
// StepoverSpacing
// ---------------------------------------------------------------------------

TEST(ToolpathGen, StepoverSpacing)
{
    // 10mm x 10mm, 1mm resolution
    Heightmap hm = makeFlatHeightmap(-2.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.stepoverPreset = StepoverPreset::Rough;  // 25% of 4mm = 1mm stepover

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // Collect unique Y positions of cut points (non-rapid)
    std::vector<f32> yPositions;
    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            bool found = false;
            for (f32 y : yPositions) {
                if (std::abs(y - pt.position.y) < 0.01f) {
                    found = true;
                    break;
                }
            }
            if (!found) yPositions.push_back(pt.position.y);
        }
    }

    // With 10mm extent and 1mm stepover, expect ~11 lines
    EXPECT_GE(yPositions.size(), 10u);

    // Check spacing between consecutive lines
    std::sort(yPositions.begin(), yPositions.end());
    for (size_t i = 1; i < yPositions.size(); ++i) {
        f32 spacing = yPositions[i] - yPositions[i - 1];
        EXPECT_NEAR(spacing, 1.0f, 0.01f)
            << "Line spacing should match stepover (1mm)";
    }
}

// ---------------------------------------------------------------------------
// AlternatingDirection
// ---------------------------------------------------------------------------

TEST(ToolpathGen, AlternatingDirection)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Alternating;
    cfg.customStepoverPct = 50.0f;  // 50% of 4mm = 2mm stepover

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // Find the first cut point of each scan line (group by Y)
    // Even lines should start at low X, odd at high X
    std::vector<std::pair<f32, f32>> lineStarts;  // (y, first_x)
    f32 lastY = -999.0f;
    for (const auto& pt : path.points) {
        if (!pt.rapid && std::abs(pt.position.y - lastY) > 0.1f) {
            lineStarts.push_back({pt.position.y, pt.position.x});
            lastY = pt.position.y;
        }
    }

    ASSERT_GE(lineStarts.size(), 2u);

    // Even-indexed lines start at low X, odd at high X (alternating)
    for (size_t i = 0; i < lineStarts.size(); ++i) {
        if (i % 2 == 0) {
            EXPECT_NEAR(lineStarts[i].second, 0.0f, 0.5f)
                << "Even line " << i << " should start at min X";
        } else {
            EXPECT_NEAR(lineStarts[i].second, 10.0f, 0.5f)
                << "Odd line " << i << " should start at max X";
        }
    }
}

// ---------------------------------------------------------------------------
// ClimbDirection
// ---------------------------------------------------------------------------

TEST(ToolpathGen, ClimbDirection)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // All scan lines should start at the same X (min X for Climb = forward)
    f32 lastY = -999.0f;
    for (const auto& pt : path.points) {
        if (!pt.rapid && std::abs(pt.position.y - lastY) > 0.1f) {
            EXPECT_NEAR(pt.position.x, 0.0f, 0.5f)
                << "Climb: all lines should start at min X";
            lastY = pt.position.y;
        }
    }
}

// ---------------------------------------------------------------------------
// XThenY
// ---------------------------------------------------------------------------

TEST(ToolpathGen, XThenY)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XThenY;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // Should have both X-scan and Y-scan points
    // X-scan: varying X, grouped Y
    // Y-scan: varying Y, grouped X
    // The path should have more points than either axis alone
    ToolpathConfig cfgX = cfg;
    cfgX.axis = ScanAxis::XOnly;
    Toolpath xOnly = gen.generateFinishing(hm, cfgX, 4.0f, defaultTool());

    ToolpathConfig cfgY = cfg;
    cfgY.axis = ScanAxis::YOnly;
    Toolpath yOnly = gen.generateFinishing(hm, cfgY, 4.0f, defaultTool());

    // XThenY should have roughly the sum of both
    EXPECT_GT(path.points.size(), xOnly.points.size());
    EXPECT_GT(path.points.size(), yOnly.points.size());
}

// ---------------------------------------------------------------------------
// SafeZRetract
// ---------------------------------------------------------------------------

TEST(ToolpathGen, SafeZRetract)
{
    Heightmap hm = makeFlatHeightmap(-3.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Alternating;
    cfg.safeZMm = 7.5f;
    cfg.customStepoverPct = 50.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // All rapid moves should be at safe Z
    for (const auto& pt : path.points) {
        if (pt.rapid) {
            EXPECT_NEAR(pt.position.z, 7.5f, 0.01f)
                << "Rapid moves should be at safe Z height";
        }
    }
}

// ---------------------------------------------------------------------------
// TimeEstimation
// ---------------------------------------------------------------------------

TEST(ToolpathGen, TimeEstimation)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 20.0f, 20.0f, 1.0f);

    // Compare two feed rates: halving feed rate should roughly double time
    ToolpathConfig cfgFast;
    cfgFast.axis = ScanAxis::XOnly;
    cfgFast.direction = MillDirection::Climb;
    cfgFast.feedRateMmMin = 2000.0f;
    cfgFast.plungeRateMmMin = 2000.0f;
    cfgFast.customStepoverPct = 25.0f;

    ToolpathConfig cfgSlow = cfgFast;
    cfgSlow.feedRateMmMin = 1000.0f;
    cfgSlow.plungeRateMmMin = 1000.0f;

    ToolpathGenerator gen;
    Toolpath fast = gen.generateFinishing(hm, cfgFast, 4.0f, defaultTool());
    Toolpath slow = gen.generateFinishing(hm, cfgSlow, 4.0f, defaultTool());

    EXPECT_GT(fast.estimatedTimeSec, 0.0f);
    EXPECT_GT(slow.estimatedTimeSec, fast.estimatedTimeSec);

    // Slow should be roughly 2x fast (not exact due to rapid moves)
    f32 ratio = slow.estimatedTimeSec / fast.estimatedTimeSec;
    EXPECT_GT(ratio, 1.5f);
    EXPECT_LT(ratio, 2.5f);
}

TEST(ToolpathGen, PlungeRateAffectsTimeEstimate)
{
    Heightmap hm = makeFlatHeightmap(-5.0f, 1.0f, 1.0f, 1.0f);

    ToolpathConfig slowPlunge;
    slowPlunge.axis = ScanAxis::XOnly;
    slowPlunge.direction = MillDirection::Climb;
    slowPlunge.safeZMm = 10.0f;
    slowPlunge.feedRateMmMin = 10000.0f;
    slowPlunge.plungeRateMmMin = 100.0f;
    slowPlunge.customStepoverPct = 100.0f;

    ToolpathConfig fastPlunge = slowPlunge;
    fastPlunge.plungeRateMmMin = 1000.0f;

    ToolpathGenerator gen;
    Toolpath slow = gen.generateFinishing(hm, slowPlunge, 4.0f, defaultTool());
    Toolpath fast = gen.generateFinishing(hm, fastPlunge, 4.0f, defaultTool());

    EXPECT_GT(slow.estimatedTimeSec, fast.estimatedTimeSec * 2.0f);
}

TEST(ToolpathGen, RapidRateAffectsTimeEstimate)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 20.0f, 20.0f, 1.0f);

    ToolpathConfig slowRapid;
    slowRapid.axis = ScanAxis::XOnly;
    slowRapid.direction = MillDirection::Climb;
    slowRapid.safeZMm = 10.0f;
    slowRapid.feedRateMmMin = 10000.0f;
    slowRapid.plungeRateMmMin = 10000.0f;
    slowRapid.rapidRateMmMin = 500.0f;
    slowRapid.customStepoverPct = 100.0f;

    ToolpathConfig fastRapid = slowRapid;
    fastRapid.rapidRateMmMin = 10000.0f;

    ToolpathGenerator gen;
    Toolpath slow = gen.generateFinishing(hm, slowRapid, 4.0f, defaultTool());
    Toolpath fast = gen.generateFinishing(hm, fastRapid, 4.0f, defaultTool());

    EXPECT_GT(slow.estimatedTimeSec, fast.estimatedTimeSec * 2.0f);
}

// ---------------------------------------------------------------------------
// EmptyHeightmap
// ---------------------------------------------------------------------------

TEST(ToolpathGen, EmptyHeightmap)
{
    Heightmap hm;  // Empty
    ToolpathConfig cfg;

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    EXPECT_TRUE(path.points.empty());
    EXPECT_FLOAT_EQ(path.totalDistanceMm, 0.0f);
}

// ===========================================================================
// Clearing Pass Tests (Plan 17-03)
// ===========================================================================

// Helper: build an IslandResult with a single island in the center of a
// heightmap grid. Island occupies cols [c0..c1], rows [r0..r1].
static IslandResult makeSingleIsland(const Heightmap& hm,
                                     int c0, int r0, int c1, int r1,
                                     f32 islandMinZ, f32 islandMaxZ)
{
    IslandResult result;
    result.maskCols = hm.cols();
    result.maskRows = hm.rows();
    result.islandMask.assign(
        static_cast<size_t>(result.maskCols * result.maskRows), -1);

    Island island;
    island.id = 0;
    island.minZ = islandMinZ;
    island.maxZ = islandMaxZ;
    island.depth = islandMaxZ - islandMinZ;

    const f32 res = hm.resolution();
    const Vec3 bmin = hm.boundsMin();

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            size_t index = static_cast<size_t>(r) *
                               static_cast<size_t>(result.maskCols) +
                           static_cast<size_t>(c);
            result.islandMask[index] = 0;
            island.cells.push_back({c, r});
        }
    }

    island.boundsMin = {bmin.x + static_cast<f32>(c0) * res,
                        bmin.y + static_cast<f32>(r0) * res};
    island.boundsMax = {bmin.x + static_cast<f32>(c1) * res,
                        bmin.y + static_cast<f32>(r1) * res};
    island.centroid = {(island.boundsMin.x + island.boundsMax.x) * 0.5f,
                       (island.boundsMin.y + island.boundsMax.y) * 0.5f};
    island.areaMm2 = (island.boundsMax.x - island.boundsMin.x) *
                      (island.boundsMax.y - island.boundsMin.y);
    island.minClearDiameter = 2.0f;

    result.islands.push_back(island);
    return result;
}

// ---------------------------------------------------------------------------
// ClearingPass_SingleIsland
// ---------------------------------------------------------------------------

TEST(ClearingPass, SingleIsland)
{
    // 10x10mm flat surface at Z=0, island in center region (cols 3-7, rows 3-7)
    Heightmap hm = makeFlatHeightmap(0.0f, 10.0f, 10.0f, 1.0f);
    IslandResult islands = makeSingleIsland(hm, 3, 3, 7, 7, -5.0f, 0.0f);

    ToolpathConfig cfg;
    cfg.safeZMm = 5.0f;
    cfg.feedRateMmMin = 1000.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateClearing(hm, islands, cfg, 4.0f);

    ASSERT_FALSE(path.points.empty());

    // Verify: no cut points outside the island bounding box (with margin)
    // Margin: tool radius (2mm) + lead-in/out ramp (2mm) + tolerance
    const f32 margin = 4.5f;
    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            EXPECT_GE(pt.position.x, islands.islands[0].boundsMin.x - margin);
            EXPECT_LE(pt.position.x, islands.islands[0].boundsMax.x + margin);
            EXPECT_GE(pt.position.y, islands.islands[0].boundsMin.y - margin);
            EXPECT_LE(pt.position.y, islands.islands[0].boundsMax.y + margin);
        }
    }
}

// ---------------------------------------------------------------------------
// ClearingPass_DeepIslandMultiPass
// ---------------------------------------------------------------------------

TEST(ClearingPass, DeepIslandMultiPass)
{
    // Deep island: depth = 12mm with 4mm tool -> 3 depth passes expected
    Heightmap hm = makeFlatHeightmap(0.0f, 10.0f, 10.0f, 1.0f);
    IslandResult islands = makeSingleIsland(hm, 3, 3, 7, 7, -12.0f, 0.0f);

    ToolpathConfig cfg;
    cfg.safeZMm = 5.0f;

    ToolpathGenerator gen;
    f32 toolDiam = 4.0f;
    Toolpath path = gen.generateClearing(hm, islands, cfg, toolDiam);

    ASSERT_FALSE(path.points.empty());

    // Count distinct depth levels among cut points (non-rapid, non-ramp)
    // We expect multiple distinct Z levels corresponding to depth passes
    std::vector<f32> depthLevels;
    for (const auto& pt : path.points) {
        if (!pt.rapid && pt.position.z < -0.1f) { // Below surface
            bool found = false;
            for (f32 z : depthLevels) {
                if (std::abs(z - pt.position.z) < 0.5f) {
                    found = true;
                    break;
                }
            }
            if (!found) depthLevels.push_back(pt.position.z);
        }
    }

    // With depth=12mm and stepdown=4mm (toolDiam), expect 3 passes
    EXPECT_GE(depthLevels.size(), 2u)
        << "Deep island should produce multiple depth passes";
}

// ---------------------------------------------------------------------------
// ClearingPass_NoIslandsNoClearingPath
// ---------------------------------------------------------------------------

TEST(ClearingPass, NoIslandsNoClearingPath)
{
    Heightmap hm = makeFlatHeightmap(0.0f, 10.0f, 10.0f, 1.0f);
    IslandResult islands; // Empty: no islands
    islands.maskCols = hm.cols();
    islands.maskRows = hm.rows();

    ToolpathConfig cfg;

    ToolpathGenerator gen;
    Toolpath path = gen.generateClearing(hm, islands, cfg, 4.0f);

    EXPECT_TRUE(path.points.empty())
        << "No islands should produce empty clearing toolpath";
}

// ---------------------------------------------------------------------------
// ClearingPass_RampEntry
// ---------------------------------------------------------------------------

TEST(ClearingPass, RampEntry)
{
    Heightmap hm = makeFlatHeightmap(0.0f, 10.0f, 10.0f, 1.0f);
    IslandResult islands = makeSingleIsland(hm, 3, 3, 7, 7, -5.0f, 0.0f);

    ToolpathConfig cfg;
    cfg.safeZMm = 5.0f;
    cfg.leadInMm = 2.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateClearing(hm, islands, cfg, 4.0f);

    ASSERT_FALSE(path.points.empty());

    // Find the first transition from rapid to cut -- should be a ramp
    // (not a direct plunge to cutting depth)
    bool foundRamp = false;
    for (size_t i = 1; i < path.points.size(); ++i) {
        if (path.points[i - 1].rapid && !path.points[i].rapid) {
            // First cut after rapid: should be at island maxZ (surface), not depth
            EXPECT_NEAR(path.points[i].position.z, 0.0f, 0.5f)
                << "Ramp entry should start near island surface (maxZ=0)";
            foundRamp = true;
            break;
        }
    }
    EXPECT_TRUE(foundRamp) << "Should find a ramp entry point";
}

// ---------------------------------------------------------------------------
// ClearingPass_MultipleIslands
// ---------------------------------------------------------------------------

TEST(ClearingPass, MultipleIslands)
{
    Heightmap hm = makeFlatHeightmap(0.0f, 20.0f, 20.0f, 1.0f);

    // Build two islands manually
    IslandResult islands;
    islands.maskCols = hm.cols();
    islands.maskRows = hm.rows();
    islands.islandMask.assign(
        static_cast<size_t>(islands.maskCols * islands.maskRows), -1);

    const f32 res = hm.resolution();
    const Vec3 bmin = hm.boundsMin();

    // Island 0: cols 2-5, rows 2-5
    Island is0;
    is0.id = 0;
    is0.minZ = -4.0f;
    is0.maxZ = 0.0f;
    is0.depth = 4.0f;
    is0.boundsMin = {bmin.x + 2.0f * res, bmin.y + 2.0f * res};
    is0.boundsMax = {bmin.x + 5.0f * res, bmin.y + 5.0f * res};
    is0.centroid = {(is0.boundsMin.x + is0.boundsMax.x) * 0.5f,
                    (is0.boundsMin.y + is0.boundsMax.y) * 0.5f};
    is0.areaMm2 = 9.0f;
    is0.minClearDiameter = 2.0f;
    for (int r = 2; r <= 5; ++r)
        for (int c = 2; c <= 5; ++c) {
            size_t index = static_cast<size_t>(r) *
                               static_cast<size_t>(islands.maskCols) +
                           static_cast<size_t>(c);
            islands.islandMask[index] = 0;
            is0.cells.push_back({c, r});
        }

    // Island 1: cols 12-16, rows 12-16
    Island is1;
    is1.id = 1;
    is1.minZ = -3.0f;
    is1.maxZ = 0.0f;
    is1.depth = 3.0f;
    is1.boundsMin = {bmin.x + 12.0f * res, bmin.y + 12.0f * res};
    is1.boundsMax = {bmin.x + 16.0f * res, bmin.y + 16.0f * res};
    is1.centroid = {(is1.boundsMin.x + is1.boundsMax.x) * 0.5f,
                    (is1.boundsMin.y + is1.boundsMax.y) * 0.5f};
    is1.areaMm2 = 16.0f;
    is1.minClearDiameter = 2.0f;
    for (int r = 12; r <= 16; ++r)
        for (int c = 12; c <= 16; ++c) {
            size_t index = static_cast<size_t>(r) *
                               static_cast<size_t>(islands.maskCols) +
                           static_cast<size_t>(c);
            islands.islandMask[index] = 1;
            is1.cells.push_back({c, r});
        }

    islands.islands.push_back(is0);
    islands.islands.push_back(is1);

    ToolpathConfig cfg;
    cfg.safeZMm = 5.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateClearing(hm, islands, cfg, 4.0f);

    ASSERT_FALSE(path.points.empty());

    // Count retract-to-safeZ moves: should have retracts between islands
    int retractCount = 0;
    for (const auto& pt : path.points) {
        if (pt.rapid && std::abs(pt.position.z - cfg.safeZMm) < 0.01f) {
            ++retractCount;
        }
    }

    // Each island gets multiple retracts (one per scan line + between islands)
    EXPECT_GT(retractCount, 2)
        << "Should have multiple retracts including between islands";
}

// ===========================================================================
// Tool Offset Compensation Tests (17-02)
// ===========================================================================

// ---------------------------------------------------------------------------
// VBitFlatSurface
// ---------------------------------------------------------------------------

TEST(ToolOffset, VBitFlatSurface)
{
    // Flat surface: V-bit tip contacts surface directly, no offset needed
    Heightmap hm = makeFlatHeightmap(-5.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;

    VtdbToolGeometry tool = makeVBit(6.35, 90.0);
    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 6.35f, tool);

    ASSERT_FALSE(path.points.empty());

    // On a perfectly flat surface, V-bit offset should be 0
    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            EXPECT_NEAR(pt.position.z, -5.0f, 0.15f)
                << "V-bit on flat surface should be at surface Z";
        }
    }
}

// ---------------------------------------------------------------------------
// BallNoseCompensation
// ---------------------------------------------------------------------------

TEST(ToolOffset, BallNoseCompensation)
{
    // Ball nose: tool center is tipRadius above contact on flat surface
    Heightmap hm = makeFlatHeightmap(-3.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;

    const f64 diameter = 6.0;
    const f32 tipRadius = 3.0f;
    VtdbToolGeometry tool = makeBallNose(diameter);
    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, static_cast<f32>(diameter), tool);

    ASSERT_FALSE(path.points.empty());

    // On flat surface, ball nose Z should be raised by tipRadius
    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            EXPECT_NEAR(pt.position.z, -3.0f + tipRadius, 0.5f)
                << "Ball nose should be raised by tip radius on flat surface";
        }
    }
}

// ---------------------------------------------------------------------------
// EndMillCompensation
// ---------------------------------------------------------------------------

TEST(ToolOffset, EndMillCompensation)
{
    // End mill on flat surface: offset should be 0
    Heightmap hm = makeFlatHeightmap(-2.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;

    VtdbToolGeometry tool = makeEndMill(6.0);
    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 6.0f, tool);

    ASSERT_FALSE(path.points.empty());

    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            EXPECT_NEAR(pt.position.z, -2.0f, 0.15f)
                << "End mill on flat surface should be at surface Z";
        }
    }
}

// ---------------------------------------------------------------------------
// NoGouging (V-bit on slope)
// ---------------------------------------------------------------------------

TEST(ToolOffset, NoGouging)
{
    const f32 widthMm = 10.0f;
    const f32 heightMm = 10.0f;
    const f32 res = 1.0f;

    // Build a ramp: Z = 0 at x=0, Z = -10 at x=10
    std::vector<Vertex> verts(4);
    verts[0].position = {0.0f, 0.0f, 0.0f};
    verts[1].position = {widthMm, 0.0f, -10.0f};
    verts[2].position = {widthMm, heightMm, -10.0f};
    verts[3].position = {0.0f, heightMm, 0.0f};

    std::vector<u32> indices = {0, 1, 2, 0, 2, 3};

    Vec3 bmin = {0.0f, 0.0f, -10.0f};
    Vec3 bmax = {widthMm, heightMm, 0.0f};

    HeightmapConfig hmCfg;
    hmCfg.resolutionMm = res;
    hmCfg.defaultZ = -10.0f;

    Heightmap hm;
    hm.build(verts, indices, bmin, bmax, hmCfg);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;

    VtdbToolGeometry tool = makeVBit(6.35, 60.0);
    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 6.35f, tool);

    ASSERT_FALSE(path.points.empty());

    // V-bit on slope should never gouge below surface
    for (const auto& pt : path.points) {
        if (!pt.rapid) {
            const f32 surfaceZ = hm.atMm(pt.position.x, pt.position.y);
            EXPECT_GE(pt.position.z, surfaceZ - 0.01f)
                << "V-bit should not gouge below surface at x="
                << pt.position.x;
        }
    }
}

// ---------------------------------------------------------------------------
// TravelLimits: Within Bounds
// ---------------------------------------------------------------------------

TEST(TravelLimits, WithinBounds)
{
    Heightmap hm = makeFlatHeightmap(1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.safeZMm = 5.0f;
    cfg.customStepoverPct = 50.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    auto warnings = gen.validateLimits(path, 100.0f, 100.0f, 50.0f);
    EXPECT_TRUE(warnings.empty())
        << "No warnings when toolpath is within travel limits";
}

// ---------------------------------------------------------------------------
// TravelLimits: Exceeds Bounds
// ---------------------------------------------------------------------------

TEST(TravelLimits, ExceedsBounds)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.safeZMm = 5.0f;
    cfg.customStepoverPct = 50.0f;

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // Travel limits too small
    auto warnings = gen.validateLimits(path, 5.0f, 5.0f, 50.0f);
    EXPECT_GE(warnings.size(), 2u)
        << "Should get warnings for X and Y axes exceeding limits";

    bool hasX = false, hasY = false;
    for (const auto& w : warnings) {
        if (w.find("X") != std::string::npos) hasX = true;
        if (w.find("Y") != std::string::npos) hasY = true;
    }
    EXPECT_TRUE(hasX) << "Should warn about X axis";
    EXPECT_TRUE(hasY) << "Should warn about Y axis";
}

// ---------------------------------------------------------------------------
// ScanResolution: Custom resolution reduces point count
// ---------------------------------------------------------------------------

TEST(ToolpathGen, ScanResolutionReducesPoints)
{
    // 10mm x 10mm at 0.5mm heightmap resolution
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 0.5f);

    ToolpathConfig cfgDefault;
    cfgDefault.axis = ScanAxis::XOnly;
    cfgDefault.direction = MillDirection::Climb;
    cfgDefault.customStepoverPct = 50.0f;
    // scanResolutionMm = 0 → uses heightmap resolution (0.5mm)

    ToolpathConfig cfgCoarse = cfgDefault;
    cfgCoarse.scanResolutionMm = 2.0f;  // 4x coarser

    ToolpathGenerator gen;
    Toolpath pathDefault = gen.generateFinishing(hm, cfgDefault, 4.0f, defaultTool());
    Toolpath pathCoarse = gen.generateFinishing(hm, cfgCoarse, 4.0f, defaultTool());

    ASSERT_FALSE(pathDefault.points.empty());
    ASSERT_FALSE(pathCoarse.points.empty());

    // Coarser resolution should produce significantly fewer points
    EXPECT_LT(pathCoarse.points.size(), pathDefault.points.size());

    // Scan line count should be the same (resolution doesn't affect stepover)
    EXPECT_EQ(pathDefault.scanLineCount, pathCoarse.scanLineCount);

    // Both should still cover the same Z values (flat surface)
    for (const auto& pt : pathCoarse.points) {
        if (!pt.rapid) {
            EXPECT_NEAR(pt.position.z, -1.0f, 0.15f);
        }
    }
}

// ---------------------------------------------------------------------------
// ScanLineCount: Tracks actual scan passes
// ---------------------------------------------------------------------------

TEST(ToolpathGen, ScanLineCountTracked)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfg;
    cfg.axis = ScanAxis::XOnly;
    cfg.direction = MillDirection::Climb;
    cfg.customStepoverPct = 50.0f;  // 50% of 4mm = 2mm stepover

    ToolpathGenerator gen;
    Toolpath path = gen.generateFinishing(hm, cfg, 4.0f, defaultTool());

    // 10mm extent / 2mm stepover + 1 = 6 scan lines
    EXPECT_EQ(path.scanLineCount, 6);

    // G-code line count should be much larger (points per line × lines)
    EXPECT_GT(path.lineCount, path.scanLineCount);
}

// ---------------------------------------------------------------------------
// ScanLineCount: XThenY doubles scan passes
// ---------------------------------------------------------------------------

TEST(ToolpathGen, ScanLineCountXThenY)
{
    Heightmap hm = makeFlatHeightmap(-1.0f, 10.0f, 10.0f, 1.0f);

    ToolpathConfig cfgX;
    cfgX.axis = ScanAxis::XOnly;
    cfgX.direction = MillDirection::Climb;
    cfgX.customStepoverPct = 50.0f;

    ToolpathConfig cfgXY = cfgX;
    cfgXY.axis = ScanAxis::XThenY;

    ToolpathGenerator gen;
    Toolpath xOnly = gen.generateFinishing(hm, cfgX, 4.0f, defaultTool());
    Toolpath xThenY = gen.generateFinishing(hm, cfgXY, 4.0f, defaultTool());

    // XThenY should have twice the scan lines (symmetric heightmap)
    EXPECT_EQ(xThenY.scanLineCount, xOnly.scanLineCount * 2);
}
