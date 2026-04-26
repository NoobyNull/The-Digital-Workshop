#pragma once

#include "../types.h"

#include <string>
#include <vector>

namespace dw {
namespace carve {

enum class ScanAxis {
    XOnly,     // Parallel lines along X
    YOnly,     // Parallel lines along Y
    XThenY,    // Two passes: X first, then Y
    YThenX     // Two passes: Y first, then X
};

enum class MillDirection {
    Climb,          // All lines in same direction
    Conventional,   // All lines in opposite direction
    Alternating     // Bidirectional (zigzag)
};

enum class CutExtents {
    Model,     // Raster within fitted model extents
    Material   // Raster across the full material blank
};

enum class StepoverPreset {
    UltraFine,  // 1% of tip diameter
    Fine,       // 8%
    Basic,      // 12%
    Rough,      // 25%
    Roughing    // 40%
};

struct ToolpathConfig {
    ScanAxis axis = ScanAxis::XOnly;
    CutExtents cutExtents = CutExtents::Model;
    MillDirection direction = MillDirection::Alternating;
    StepoverPreset stepoverPreset = StepoverPreset::Basic;
    f32 customStepoverPct = 0.0f;  // If non-zero, overrides preset
    f32 safeZMm = 5.0f;
    f32 feedRateMmMin = 1000.0f;
    f32 plungeRateMmMin = 300.0f;
    f32 rapidRateMmMin = 5000.0f;
    f32 stepdownMm = 1.0f;
    f32 leadInMm = 2.0f;  // Ramp distance for clearing lead-in/out
    f32 scanResolutionMm = 0.0f;  // Point spacing along scan lines (0 = heightmap resolution)
};

// Single toolpath move
struct ToolpathPoint {
    Vec3 position;
    bool rapid = false;  // G0 (true) vs G1 (false)
};

// Complete toolpath
struct Toolpath {
    std::vector<ToolpathPoint> points;
    f32 totalDistanceMm = 0.0f;
    f32 estimatedTimeSec = 0.0f;
    int lineCount = 0;       // Number of G-code lines this will produce
    int scanLineCount = 0;   // Number of actual scan passes
    std::vector<std::string> warnings;  // Travel limit violations
};

// Combined clearing + finishing toolpath
struct MultiPassToolpath {
    Toolpath clearing;   // Run first (if needed)
    Toolpath finishing;   // Run second
    f32 totalTimeSec = 0.0f;
    int totalLineCount = 0;
    bool requiresToolChange = false;
    std::string clearingToolName;
    std::string finishingToolName;
};

// Convert preset to percentage
f32 stepoverPercent(StepoverPreset preset);

inline bool isPlungeFeedMove(const Vec3& previous,
                             bool previousRapid,
                             const Vec3& current)
{
    constexpr f32 kEpsilon = 0.001f;
    const f32 dx = current.x - previous.x;
    const f32 dy = current.y - previous.y;
    const f32 dz = current.z - previous.z;
    const f32 lateralSq = dx * dx + dy * dy;
    return dz < -kEpsilon &&
           (previousRapid || lateralSq <= kEpsilon * kEpsilon);
}

inline f32 linearMoveFeedRateMmMin(const Vec3& previous,
                                   bool previousRapid,
                                   const Vec3& current,
                                   const ToolpathConfig& config)
{
    if (config.plungeRateMmMin > 0.0f &&
        isPlungeFeedMove(previous, previousRapid, current)) {
        return config.plungeRateMmMin;
    }
    return config.feedRateMmMin;
}

} // namespace carve
} // namespace dw
