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

Toolpath ToolpathGenerator::generateFixedDepthRaster(const Vec3& boundsMin,
                                                      const Vec3& boundsMax,
                                                      const ToolpathConfig& config,
                                                      f32 toolDiameter,
                                                      f32 depthMm)
{
    Toolpath path;
    if (toolDiameter <= 0.0f || depthMm <= 0.0f) return path;

    const Vec3 bmin{
        std::min(boundsMin.x, boundsMax.x),
        std::min(boundsMin.y, boundsMax.y),
        0.0f
    };
    const Vec3 bmax{
        std::max(boundsMin.x, boundsMax.x),
        std::max(boundsMin.y, boundsMax.y),
        0.0f
    };
    if (bmax.x <= bmin.x || bmax.y <= bmin.y) return path;

    const f32 pct = (config.customStepoverPct > 0.0f)
                        ? config.customStepoverPct
                        : stepoverPercent(config.stepoverPreset);
    const f32 stepoverMm = toolDiameter * pct / 100.0f;
    if (stepoverMm <= 0.0f) return path;

    const f32 stepdownMm = (config.stepdownMm > 0.0f)
                               ? config.stepdownMm
                               : std::max(0.25f, toolDiameter * 0.5f);
    const int passCount = std::max(
        1, static_cast<int>(std::ceil(depthMm / stepdownMm)));

    auto generateAxis = [&](bool primaryAxis, f32 cutZ) {
        generateFixedDepthScanLines(path, bmin, bmax, config, stepoverMm,
                                    cutZ, primaryAxis);
    };

    for (int passIdx = 0; passIdx < passCount; ++passIdx) {
        const f32 passDepth = std::min(
            depthMm, stepdownMm * static_cast<f32>(passIdx + 1));
        const f32 cutZ = -passDepth;

        switch (config.axis) {
            case ScanAxis::XOnly:
                generateAxis(true, cutZ);
                break;
            case ScanAxis::YOnly:
                generateAxis(false, cutZ);
                break;
            case ScanAxis::XThenY:
                generateAxis(true, cutZ);
                generateAxis(false, cutZ);
                break;
            case ScanAxis::YThenX:
                generateAxis(false, cutZ);
                generateAxis(true, cutZ);
                break;
        }
    }

    computeMetrics(path, config);
    return path;
}

Toolpath ToolpathGenerator::generateFixedDepthClearingAroundModel(
    const Vec3& stockMin,
    const Vec3& stockMax,
    const Vec3& modelMin,
    const Vec3& modelMax,
    const ToolpathConfig& config,
    f32 toolDiameter,
    f32 depthMm)
{
    Toolpath path;
    if (toolDiameter <= 0.0f || depthMm <= 0.0f) return path;

    const Vec3 smin{
        std::min(stockMin.x, stockMax.x),
        std::min(stockMin.y, stockMax.y),
        0.0f
    };
    const Vec3 smax{
        std::max(stockMin.x, stockMax.x),
        std::max(stockMin.y, stockMax.y),
        0.0f
    };
    if (smax.x <= smin.x || smax.y <= smin.y) return path;

    const Vec3 mmin{
        std::min(modelMin.x, modelMax.x),
        std::min(modelMin.y, modelMax.y),
        0.0f
    };
    const Vec3 mmax{
        std::max(modelMin.x, modelMax.x),
        std::max(modelMin.y, modelMax.y),
        0.0f
    };
    if (mmax.x <= mmin.x || mmax.y <= mmin.y) return path;

    const f32 pct = (config.customStepoverPct > 0.0f)
                        ? config.customStepoverPct
                        : stepoverPercent(config.stepoverPreset);
    const f32 stepoverMm = toolDiameter * pct / 100.0f;
    if (stepoverMm <= 0.0f) return path;

    const f32 toolRadius = toolDiameter * 0.5f;
    const Vec3 expandedNoCutMin{mmin.x - toolRadius, mmin.y - toolRadius, 0.0f};
    const Vec3 expandedNoCutMax{mmax.x + toolRadius, mmax.y + toolRadius, 0.0f};
    const bool hasNoCutRegion =
        expandedNoCutMax.x > smin.x && expandedNoCutMin.x < smax.x &&
        expandedNoCutMax.y > smin.y && expandedNoCutMin.y < smax.y;

    const Vec3 noCutMin{
        std::clamp(expandedNoCutMin.x, smin.x, smax.x),
        std::clamp(expandedNoCutMin.y, smin.y, smax.y),
        0.0f
    };
    const Vec3 noCutMax{
        std::clamp(expandedNoCutMax.x, smin.x, smax.x),
        std::clamp(expandedNoCutMax.y, smin.y, smax.y),
        0.0f
    };

    if (hasNoCutRegion &&
        noCutMin.x <= smin.x && noCutMax.x >= smax.x &&
        noCutMin.y <= smin.y && noCutMax.y >= smax.y) {
        return path;
    }

    const f32 stepdownMm = (config.stepdownMm > 0.0f)
                               ? config.stepdownMm
                               : std::max(0.25f, toolDiameter * 0.5f);
    const int passCount = std::max(
        1, static_cast<int>(std::ceil(depthMm / stepdownMm)));

    auto generateAxis = [&](bool primaryAxis, f32 cutZ) {
        generateFixedDepthClearingScanLines(path, smin, smax,
                                            noCutMin, noCutMax,
                                            hasNoCutRegion, config,
                                            stepoverMm, cutZ, primaryAxis);
    };

    for (int passIdx = 0; passIdx < passCount; ++passIdx) {
        const f32 passDepth = std::min(
            depthMm, stepdownMm * static_cast<f32>(passIdx + 1));
        const f32 cutZ = -passDepth;

        switch (config.axis) {
            case ScanAxis::XOnly:
                generateAxis(true, cutZ);
                break;
            case ScanAxis::YOnly:
                generateAxis(false, cutZ);
                break;
            case ScanAxis::XThenY:
                generateAxis(true, cutZ);
                generateAxis(false, cutZ);
                break;
            case ScanAxis::YThenX:
                generateAxis(false, cutZ);
                generateAxis(true, cutZ);
                break;
        }
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

void ToolpathGenerator::generateFixedDepthScanLines(Toolpath& path,
                                                     const Vec3& boundsMin,
                                                     const Vec3& boundsMax,
                                                     const ToolpathConfig& config,
                                                     f32 stepoverMm,
                                                     f32 cutZ,
                                                     bool primaryAxis)
{
    const f32 res = (config.scanResolutionMm > 0.0f)
                        ? config.scanResolutionMm
                        : std::max(0.25f, stepoverMm);

    const f32 scanMin = primaryAxis ? boundsMin.x : boundsMin.y;
    const f32 scanMax = primaryAxis ? boundsMax.x : boundsMax.y;
    const f32 stepMin = primaryAxis ? boundsMin.y : boundsMin.x;
    const f32 stepMax = primaryAxis ? boundsMax.y : boundsMax.x;

    const f32 scanExtent = scanMax - scanMin;
    const f32 stepExtent = stepMax - stepMin;
    if (scanExtent <= 0.0f || stepoverMm <= 0.0f || res <= 0.0f) return;

    const int lineCount =
        std::max(1, static_cast<int>(stepExtent / stepoverMm) + 1);
    path.scanLineCount += lineCount;

    const int pointCount =
        std::max(2, static_cast<int>(scanExtent / res) + 1);

    for (int lineIdx = 0; lineIdx < lineCount; ++lineIdx) {
        const f32 stepPos = std::min(
            stepMax, stepMin + static_cast<f32>(lineIdx) * stepoverMm);

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

        const f32 startScan = forward ? scanMin : scanMax;
        Vec3 startPos = primaryAxis
            ? Vec3{startScan, stepPos, config.safeZMm}
            : Vec3{stepPos, startScan, config.safeZMm};

        addRetract(path, config.safeZMm);
        addRapidTo(path, startPos);
        startPos.z = cutZ;
        addCutTo(path, startPos);

        for (int ptIdx = 1; ptIdx < pointCount; ++ptIdx) {
            const int idx = forward ? ptIdx : (pointCount - 1 - ptIdx);
            const f32 t = static_cast<f32>(idx) /
                          static_cast<f32>(pointCount - 1);
            const f32 scanPos = scanMin + t * scanExtent;

            const f32 x = primaryAxis ? scanPos : stepPos;
            const f32 y = primaryAxis ? stepPos : scanPos;
            addCutTo(path, {x, y, cutZ});
        }
    }
}

void ToolpathGenerator::generateFixedDepthClearingScanLines(
    Toolpath& path,
    const Vec3& stockMin,
    const Vec3& stockMax,
    const Vec3& noCutMin,
    const Vec3& noCutMax,
    bool hasNoCutRegion,
    const ToolpathConfig& config,
    f32 stepoverMm,
    f32 cutZ,
    bool primaryAxis)
{
    const f32 res = (config.scanResolutionMm > 0.0f)
                        ? config.scanResolutionMm
                        : std::max(0.25f, stepoverMm);

    const f32 scanMin = primaryAxis ? stockMin.x : stockMin.y;
    const f32 scanMax = primaryAxis ? stockMax.x : stockMax.y;
    const f32 stepMin = primaryAxis ? stockMin.y : stockMin.x;
    const f32 stepMax = primaryAxis ? stockMax.y : stockMax.x;

    const f32 noCutScanMin = primaryAxis ? noCutMin.x : noCutMin.y;
    const f32 noCutScanMax = primaryAxis ? noCutMax.x : noCutMax.y;
    const f32 noCutStepMin = primaryAxis ? noCutMin.y : noCutMin.x;
    const f32 noCutStepMax = primaryAxis ? noCutMax.y : noCutMax.x;

    const f32 scanExtent = scanMax - scanMin;
    const f32 stepExtent = stepMax - stepMin;
    if (scanExtent <= 0.0f || stepoverMm <= 0.0f || res <= 0.0f) return;

    const int lineCount =
        std::max(1, static_cast<int>(stepExtent / stepoverMm) + 1);

    const auto cutSegment = [&](f32 segMin, f32 segMax, f32 stepPos,
                                bool forward) {
        constexpr f32 kEpsilon = 0.001f;
        segMin = std::clamp(segMin, scanMin, scanMax);
        segMax = std::clamp(segMax, scanMin, scanMax);
        if (segMax - segMin <= kEpsilon) return;

        const f32 segmentStart = forward ? segMin : segMax;
        const f32 segmentEnd = forward ? segMax : segMin;
        const f32 segmentExtent = std::abs(segmentEnd - segmentStart);
        const int pointCount =
            std::max(2, static_cast<int>(segmentExtent / res) + 1);

        Vec3 startPos = primaryAxis
            ? Vec3{segmentStart, stepPos, config.safeZMm}
            : Vec3{stepPos, segmentStart, config.safeZMm};

        addRetract(path, config.safeZMm);
        addRapidTo(path, startPos);
        startPos.z = cutZ;
        addCutTo(path, startPos);

        for (int ptIdx = 1; ptIdx < pointCount; ++ptIdx) {
            const f32 t = static_cast<f32>(ptIdx) /
                          static_cast<f32>(pointCount - 1);
            const f32 scanPos =
                segmentStart + t * (segmentEnd - segmentStart);

            const f32 x = primaryAxis ? scanPos : stepPos;
            const f32 y = primaryAxis ? stepPos : scanPos;
            addCutTo(path, {x, y, cutZ});
        }

        ++path.scanLineCount;
    };

    for (int lineIdx = 0; lineIdx < lineCount; ++lineIdx) {
        const f32 stepPos = std::min(
            stepMax, stepMin + static_cast<f32>(lineIdx) * stepoverMm);

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

        std::vector<std::pair<f32, f32>> intervals;
        const bool crossesNoCutStep =
            hasNoCutRegion &&
            stepPos > noCutStepMin && stepPos < noCutStepMax;
        if (crossesNoCutStep) {
            intervals.push_back({scanMin, noCutScanMin});
            intervals.push_back({noCutScanMax, scanMax});
        } else {
            intervals.push_back({scanMin, scanMax});
        }

        if (forward) {
            for (const auto& interval : intervals) {
                cutSegment(interval.first, interval.second, stepPos, true);
            }
        } else {
            for (auto it = intervals.rbegin(); it != intervals.rend(); ++it) {
                cutSegment(it->first, it->second, stepPos, false);
            }
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
