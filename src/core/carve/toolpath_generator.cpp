#include "toolpath_generator.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dw {
namespace carve {

f32 stepoverPercent(StepoverPreset preset)
{
    switch (preset) {
        case StepoverPreset::UltraFine: return 1.0f;
        case StepoverPreset::Fine:      return 8.0f;
        case StepoverPreset::Basic:     return 12.0f;
        case StepoverPreset::Rough:     return 25.0f;
        case StepoverPreset::Roughing:  return 40.0f;
    }
    return 12.0f;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Toolpath ToolpathGenerator::generateFinishing(const Heightmap& heightmap,
                                               const ToolpathConfig& config,
                                               f32 toolTipDiameter,
                                               const VtdbToolGeometry& tool)
{
    Toolpath path;
    if (heightmap.empty() || toolTipDiameter <= 0.0f) return path;

    f32 pct = (config.customStepoverPct > 0.0f)
                  ? config.customStepoverPct
                  : stepoverPercent(config.stepoverPreset);
    f32 stepoverMm = toolTipDiameter * pct / 100.0f;
    if (stepoverMm <= 0.0f) return path;

    switch (config.axis) {
        case ScanAxis::XOnly:
            generateScanLines(path, heightmap, config, stepoverMm, true);
            break;
        case ScanAxis::YOnly:
            generateScanLines(path, heightmap, config, stepoverMm, false);
            break;
        case ScanAxis::XThenY:
            generateScanLines(path, heightmap, config, stepoverMm, true);
            generateScanLines(path, heightmap, config, stepoverMm, false);
            break;
        case ScanAxis::YThenX:
            generateScanLines(path, heightmap, config, stepoverMm, false);
            generateScanLines(path, heightmap, config, stepoverMm, true);
            break;
    }

    // Apply tool offset compensation to all cutting points
    for (auto& pt : path.points) {
        if (!pt.rapid) {
            f32 offset = toolOffset(heightmap, pt.position.x, pt.position.y, tool);
            pt.position.z += offset;
        }
    }

    computeMetrics(path, config);
    return path;
}

Toolpath ToolpathGenerator::generateClearing(const Heightmap& heightmap,
                                              const IslandResult& islands,
                                              const ToolpathConfig& config,
                                              f32 toolDiameter)
{
    Toolpath path;
    if (heightmap.empty() || toolDiameter <= 0.0f) return path;
    if (islands.islands.empty()) return path;

    const f32 stepoverMm = toolDiameter * 0.4f; // 40% stepover for roughing
    const f32 stepdownMm = toolDiameter;         // Max depth per pass
    const f32 toolRadius = toolDiameter * 0.5f;

    for (const auto& island : islands.islands) {
        addRetract(path, config.safeZMm);
        clearIslandRegion(path, heightmap, islands, island,
                          config, stepoverMm, stepdownMm, toolRadius);
    }

    computeMetrics(path, config);
    return path;
}

// ---------------------------------------------------------------------------
// Island clearing
// ---------------------------------------------------------------------------

void ToolpathGenerator::clearIslandRegion(Toolpath& path,
                                           const Heightmap& heightmap,
                                           const IslandResult& islands,
                                           const Island& island,
                                           const ToolpathConfig& config,
                                           f32 stepoverMm,
                                           f32 stepdownMm,
                                           f32 toolRadius)
{
    const f32 res = heightmap.resolution();
    const Vec3 bmin = heightmap.boundsMin();

    // Island bounding box with tool radius margin
    const f32 xMin = island.boundsMin.x - toolRadius;
    const f32 xMax = island.boundsMax.x + toolRadius;
    const f32 yMin = island.boundsMin.y - toolRadius;
    const f32 yMax = island.boundsMax.y + toolRadius;

    // Depth passes: from surface (maxZ) down to clearing depth
    const f32 margin = 0.2f; // Small margin above island floor
    const f32 targetZ = island.minZ + margin;
    const int numDepthPasses = std::max(
        1, static_cast<int>(std::ceil(island.depth / stepdownMm)));

    for (int depthIdx = 0; depthIdx < numDepthPasses; ++depthIdx) {
        const f32 t = static_cast<f32>(depthIdx + 1)
                      / static_cast<f32>(numDepthPasses);
        const f32 cutZ = island.maxZ - t * (island.maxZ - targetZ);

        // Raster scan lines within island bounds along X
        const int numLines = std::max(
            1, static_cast<int>((yMax - yMin) / stepoverMm) + 1);

        for (int lineIdx = 0; lineIdx < numLines; ++lineIdx) {
            const f32 y = yMin + static_cast<f32>(lineIdx) * stepoverMm;
            if (y > yMax) break;

            bool inIsland = false;
            addRetract(path, config.safeZMm);

            const int numPoints = std::max(
                1, static_cast<int>((xMax - xMin) / res) + 1);

            for (int ptIdx = 0; ptIdx < numPoints; ++ptIdx) {
                const f32 x = xMin + static_cast<f32>(ptIdx) * res;

                // Check island mask membership
                const int col = static_cast<int>((x - bmin.x) / res);
                const int row = static_cast<int>((y - bmin.y) / res);
                bool cellInIsland = false;
                if (col >= 0 && col < islands.maskCols &&
                    row >= 0 && row < islands.maskRows) {
                    size_t maskIndex = static_cast<size_t>(row) *
                                           static_cast<size_t>(islands.maskCols) +
                                       static_cast<size_t>(col);
                    cellInIsland =
                        (islands.islandMask[maskIndex] == island.id);
                }

                if (cellInIsland && !inIsland) {
                    // Entering island: ramp down over lead-in distance
                    addRapidTo(path, {x, y, config.safeZMm});
                    const f32 rampEnd = std::min(
                        x + config.leadInMm, xMax);
                    addCutTo(path, {x, y, island.maxZ});
                    addCutTo(path, {rampEnd, y, cutZ});
                    inIsland = true;
                } else if (cellInIsland && inIsland) {
                    // Inside island: cut at depth or surface (whichever higher)
                    const f32 surfaceZ = heightmap.atMm(x, y);
                    const f32 z = std::max(cutZ, surfaceZ);
                    addCutTo(path, {x, y, z});
                } else if (!cellInIsland && inIsland) {
                    // Exiting island: ramp up over lead-out distance
                    addCutTo(path, {x, y, island.maxZ});
                    addCutTo(path, {x + config.leadInMm, y, config.safeZMm});
                    inIsland = false;
                }
                // Not in island and not entering: skip (no cut)
            }

            if (inIsland) {
                addRetract(path, config.safeZMm);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Scan-line generation
// ---------------------------------------------------------------------------

void ToolpathGenerator::generateScanLines(Toolpath& path,
                                           const Heightmap& heightmap,
                                           const ToolpathConfig& config,
                                           f32 stepoverMm,
                                           bool primaryAxis)
{
    const Vec3 bmin = heightmap.boundsMin();
    const Vec3 bmax = heightmap.boundsMax();
    const f32 hmRes = heightmap.resolution();
    const f32 res = (config.scanResolutionMm > 0.0f)
                        ? config.scanResolutionMm
                        : hmRes;

    // Axis mapping:
    //   primaryAxis==true  -> scan along X, step along Y
    //   primaryAxis==false -> scan along Y, step along X
    const f32 scanMin  = primaryAxis ? bmin.x : bmin.y;
    const f32 scanMax  = primaryAxis ? bmax.x : bmax.y;
    const f32 stepMin  = primaryAxis ? bmin.y : bmin.x;
    const f32 stepMax  = primaryAxis ? bmax.y : bmax.x;

    const f32 stepExtent = stepMax - stepMin;
    if (stepExtent <= 0.0f || stepoverMm <= 0.0f) return;

    const int numLines = std::max(1, static_cast<int>(stepExtent / stepoverMm) + 1);
    path.scanLineCount += numLines;

    for (int lineIdx = 0; lineIdx < numLines; ++lineIdx) {
        const f32 stepPos = stepMin + static_cast<f32>(lineIdx) * stepoverMm;
        if (stepPos > stepMax) break;

        // Determine scan direction for this line
        bool forward = true;
        switch (config.direction) {
            case MillDirection::Climb:
                forward = true;
                break;
            case MillDirection::Conventional:
                forward = false;
                break;
            case MillDirection::Alternating:
                forward = (lineIdx % 2 == 0);
                break;
        }

        // Retract before moving to next line
        addRetract(path, config.safeZMm);

        // Rapid to start of line
        f32 startScan = forward ? scanMin : scanMax;
        Vec3 startPos;
        if (primaryAxis) {
            startPos = {startScan, stepPos, config.safeZMm};
        } else {
            startPos = {stepPos, startScan, config.safeZMm};
        }
        addRapidTo(path, startPos);

        // Generate points along scan line at heightmap resolution
        const int numPoints = std::max(
            1, static_cast<int>((scanMax - scanMin) / res) + 1);

        for (int ptIdx = 0; ptIdx < numPoints; ++ptIdx) {
            const int idx = forward ? ptIdx : (numPoints - 1 - ptIdx);
            const f32 scanPos = scanMin + static_cast<f32>(idx) * res;

            f32 x = primaryAxis ? scanPos : stepPos;
            f32 y = primaryAxis ? stepPos : scanPos;
            f32 z = heightmap.atMm(x, y);

            addCutTo(path, {x, y, z});
        }
    }
}

// ---------------------------------------------------------------------------
// Point helpers
// ---------------------------------------------------------------------------

void ToolpathGenerator::addRetract(Toolpath& path, f32 safeZ)
{
    if (!path.points.empty()) {
        Vec3 pos = path.points.back().position;
        pos.z = safeZ;
        addRapidTo(path, pos);
    }
}

void ToolpathGenerator::addRapidTo(Toolpath& path, const Vec3& pos)
{
    path.points.push_back({pos, true});
}

void ToolpathGenerator::addCutTo(Toolpath& path, const Vec3& pos)
{
    path.points.push_back({pos, false});
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

void ToolpathGenerator::computeMetrics(Toolpath& path,
                                        const ToolpathConfig& config)
{
    if (path.points.size() < 2) return;

    f32 totalDist = 0.0f;
    f32 totalTimeSec = 0.0f;
    int gcodeLines = 0;

    for (size_t i = 1; i < path.points.size(); ++i) {
        const auto& prev = path.points[i - 1];
        const auto& curr = path.points[i];

        const Vec3 delta = curr.position - prev.position;
        const f32 dist = std::sqrt(delta.x * delta.x +
                                   delta.y * delta.y +
                                   delta.z * delta.z);
        totalDist += dist;

        if (dist > 0.0f) {
            f32 rate = config.rapidRateMmMin;
            if (!curr.rapid) {
                rate = linearMoveFeedRateMmMin(prev.position, prev.rapid,
                                               curr.position, config);
            }
            rate = std::max(rate, 1.0f);
            totalTimeSec += (dist / rate) * 60.0f;  // rate is mm/min
            ++gcodeLines;
        }
    }

    path.totalDistanceMm = totalDist;
    path.estimatedTimeSec = totalTimeSec;
    path.lineCount = gcodeLines;
}

// ---------------------------------------------------------------------------
// Tool offset compensation
// ---------------------------------------------------------------------------

f32 ToolpathGenerator::toolOffset(const Heightmap& heightmap,
                                   f32 x, f32 y,
                                   const VtdbToolGeometry& tool) const
{
    switch (tool.tool_type) {
        case VtdbToolType::VBit:
            return vBitOffset(heightmap, x, y, tool);
        case VtdbToolType::BallNose:
        case VtdbToolType::TaperedBallNose:
            return ballNoseOffset(heightmap, x, y, tool);
        case VtdbToolType::EndMill:
        default:
            return endMillOffset(heightmap, x, y, tool);
    }
}

f32 ToolpathGenerator::vBitOffset(const Heightmap& heightmap,
                                   f32 x, f32 y,
                                   const VtdbToolGeometry& tool) const
{
    // V-bit: tip contacts surface directly. The cone flares upward from
    // the tip at half-angle = included_angle / 2. At XY distance r from
    // the tip, the cone body is at Z = tipZ + r / tan(halfAngle).
    // If a neighbor's surface is ABOVE the cone body at that distance,
    // the tool must be raised to prevent the shank/cone from colliding.
    const f32 halfAngle = static_cast<f32>(tool.included_angle) * 0.5f;
    if (halfAngle <= 0.0f || halfAngle >= 90.0f) return 0.0f;

    const f32 tanHalf = std::tan(halfAngle * 3.14159265f / 180.0f);
    if (tanHalf <= 0.0f) return 0.0f;

    const f32 res = heightmap.resolution();
    const Vec3 bmin = heightmap.boundsMin();
    const Vec3 bmax = heightmap.boundsMax();
    const f32 centerZ = heightmap.atMm(x, y);
    f32 maxRaise = 0.0f;

    // Check 8 neighbors at heightmap resolution distance
    constexpr f32 dirs[][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    for (const auto& d : dirs) {
        const f32 nx = x + d[0] * res;
        const f32 ny = y + d[1] * res;

        // Skip neighbors outside heightmap bounds
        if (nx < bmin.x || nx > bmax.x ||
            ny < bmin.y || ny > bmax.y) continue;

        const f32 dx = d[0] * res;
        const f32 dy = d[1] * res;
        const f32 dist = std::sqrt(dx * dx + dy * dy);
        const f32 nz = heightmap.atMm(nx, ny);
        // Cone body Z at this distance from tip (flares upward)
        const f32 coneZ = centerZ + dist / tanHalf;
        if (nz > coneZ) {
            // Neighbor surface is above the cone body -- must raise
            const f32 raise = nz - coneZ;
            maxRaise = std::max(maxRaise, raise);
        }
    }
    return maxRaise;
}

f32 ToolpathGenerator::ballNoseOffset(const Heightmap& heightmap,
                                       f32 x, f32 y,
                                       const VtdbToolGeometry& tool) const
{
    // Ball nose: tool center is tipRadius above contact point.
    // Drop-cutter: find maximum of (heightmap(x+dx, y+dy) + sqrt(R^2 - dx^2 - dy^2))
    // for all points within radius R. The offset is result - centerZ.
    const f32 R = static_cast<f32>(tool.tip_radius > 0.0
                                       ? tool.tip_radius
                                       : tool.diameter * 0.5);
    if (R <= 0.0f) return 0.0f;

    const f32 res = heightmap.resolution();
    const Vec3 bmin = heightmap.boundsMin();
    const Vec3 bmax = heightmap.boundsMax();
    const int steps = std::max(1, static_cast<int>(R / res));
    const f32 centerZ = heightmap.atMm(x, y);

    // On a flat surface, the offset is R (tool center above contact by R)
    f32 maxContactZ = centerZ + R;

    for (int di = -steps; di <= steps; ++di) {
        for (int dj = -steps; dj <= steps; ++dj) {
            const f32 dx = static_cast<f32>(di) * res;
            const f32 dy = static_cast<f32>(dj) * res;
            const f32 r2 = dx * dx + dy * dy;
            if (r2 > R * R) continue;

            const f32 nx = x + dx;
            const f32 ny = y + dy;
            if (nx < bmin.x || nx > bmax.x ||
                ny < bmin.y || ny > bmax.y) continue;

            const f32 sz = heightmap.atMm(nx, ny);
            const f32 lift = std::sqrt(R * R - r2);
            const f32 contactZ = sz + lift;
            maxContactZ = std::max(maxContactZ, contactZ);
        }
    }

    // Offset = tool center Z - raw heightmap Z at center
    return maxContactZ - centerZ;
}

f32 ToolpathGenerator::endMillOffset(const Heightmap& heightmap,
                                      f32 x, f32 y,
                                      const VtdbToolGeometry& tool) const
{
    // End mill: flat bottom, tool center at max Z within tool radius circle
    const f32 R = static_cast<f32>(tool.diameter * 0.5);
    if (R <= 0.0f) return 0.0f;

    const f32 res = heightmap.resolution();
    const Vec3 bmin = heightmap.boundsMin();
    const Vec3 bmax = heightmap.boundsMax();
    const int steps = std::max(1, static_cast<int>(R / res));
    const f32 centerZ = heightmap.atMm(x, y);
    f32 maxZ = centerZ;

    for (int di = -steps; di <= steps; ++di) {
        for (int dj = -steps; dj <= steps; ++dj) {
            const f32 dx = static_cast<f32>(di) * res;
            const f32 dy = static_cast<f32>(dj) * res;
            if (dx * dx + dy * dy > R * R) continue;

            const f32 nx = x + dx;
            const f32 ny = y + dy;
            if (nx < bmin.x || nx > bmax.x ||
                ny < bmin.y || ny > bmax.y) continue;

            maxZ = std::max(maxZ, heightmap.atMm(nx, ny));
        }
    }

    return maxZ - centerZ;
}

// ---------------------------------------------------------------------------
// Travel limit validation
// ---------------------------------------------------------------------------

std::vector<std::string> ToolpathGenerator::validateLimits(
    const Toolpath& path,
    f32 travelX, f32 travelY, f32 travelZ) const
{
    std::set<std::string> seen;
    std::vector<std::string> warnings;

    for (const auto& pt : path.points) {
        const Vec3& p = pt.position;
        if ((p.x < 0.0f || p.x > travelX) && seen.find("X") == seen.end()) {
            seen.insert("X");
            warnings.push_back("X axis exceeds travel limit (" +
                               std::to_string(travelX) + " mm)");
        }
        if ((p.y < 0.0f || p.y > travelY) && seen.find("Y") == seen.end()) {
            seen.insert("Y");
            warnings.push_back("Y axis exceeds travel limit (" +
                               std::to_string(travelY) + " mm)");
        }
        if ((p.z < 0.0f || p.z > travelZ) && seen.find("Z") == seen.end()) {
            seen.insert("Z");
            warnings.push_back("Z axis exceeds travel limit (" +
                               std::to_string(travelZ) + " mm)");
        }
        if (seen.size() == 3) break;  // All axes reported
    }

    return warnings;
}

} // namespace carve
} // namespace dw
