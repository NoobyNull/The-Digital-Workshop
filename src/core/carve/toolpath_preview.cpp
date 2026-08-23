#include "toolpath_preview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace dw::carve {
namespace {

constexpr f32 kPointEpsilon = 0.0001f;
constexpr int kOrientationBuckets = 8;

bool sameTopViewPoint(const Vec3& lhs, const Vec3& rhs)
{
    return std::abs(lhs.x - rhs.x) <= kPointEpsilon &&
           std::abs(lhs.y - rhs.y) <= kPointEpsilon;
}

bool topViewCollinear(const Vec3& first, const Vec3& middle, const Vec3& last)
{
    const f32 abX = middle.x - first.x;
    const f32 abY = middle.y - first.y;
    const f32 bcX = last.x - middle.x;
    const f32 bcY = last.y - middle.y;
    const f32 cross = abX * bcY - abY * bcX;
    const f32 scale = std::max(1.0f,
                               std::hypot(abX, abY) * std::hypot(bcX, bcY));
    const f32 dot = abX * bcX + abY * bcY;
    return std::abs(cross) <= kPointEpsilon * scale && dot >= 0.0f;
}

std::vector<Vec3> simplifyTopViewPolyline(const std::vector<Vec3>& points)
{
    std::vector<Vec3> simplified;
    simplified.reserve(std::min<std::size_t>(points.size(), 16));

    for (const Vec3& point : points) {
        if (!simplified.empty() && sameTopViewPoint(simplified.back(), point)) {
            // Z is not visible in this top-down preview. Keep the latest point
            // so the endpoint still represents the source motion faithfully.
            simplified.back() = point;
            continue;
        }

        while (simplified.size() >= 2 &&
               topViewCollinear(simplified[simplified.size() - 2],
                                simplified.back(),
                                point)) {
            simplified.pop_back();
        }
        simplified.push_back(point);
    }

    return simplified;
}

struct DisplayCell {
    std::int64_t x = 0;
    std::int64_t y = 0;
    int orientation = 0;

    bool operator==(const DisplayCell& other) const
    {
        return x == other.x && y == other.y && orientation == other.orientation;
    }
};

struct DisplayCellHash {
    std::size_t operator()(const DisplayCell& cell) const
    {
        std::size_t seed = std::hash<std::int64_t>{}(cell.x);
        seed ^= std::hash<std::int64_t>{}(cell.y) + 0x9e3779b9U +
                (seed << 6U) + (seed >> 2U);
        seed ^= std::hash<int>{}(cell.orientation) + 0x9e3779b9U +
                (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

DisplayCell displayCellFor(const ToolpathPreviewStroke& stroke,
                           f32 pixelsPerMmX,
                           f32 pixelsPerMmY,
                           f32 spacingPixels)
{
    f32 minX = std::numeric_limits<f32>::max();
    f32 minY = std::numeric_limits<f32>::max();
    f32 maxX = std::numeric_limits<f32>::lowest();
    f32 maxY = std::numeric_limits<f32>::lowest();
    f32 longestDx = 0.0f;
    f32 longestDy = 0.0f;
    f32 longestLengthSq = -1.0f;

    for (std::size_t i = 0; i < stroke.cutPoints.size(); ++i) {
        const Vec3& point = stroke.cutPoints[i];
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);

        if (i == 0) continue;
        const f32 dx = (point.x - stroke.cutPoints[i - 1].x) * pixelsPerMmX;
        const f32 dy = (point.y - stroke.cutPoints[i - 1].y) * pixelsPerMmY;
        const f32 lengthSq = dx * dx + dy * dy;
        if (lengthSq > longestLengthSq) {
            longestLengthSq = lengthSq;
            longestDx = dx;
            longestDy = dy;
        }
    }

    constexpr f32 kPi = 3.14159265358979323846f;
    f32 angle = std::atan2(longestDy, longestDx);
    if (angle < 0.0f) angle += kPi;
    if (angle >= kPi) angle -= kPi;
    const int orientation = std::clamp(
        static_cast<int>(std::floor(angle * kOrientationBuckets / kPi)),
        0,
        kOrientationBuckets - 1);

    const f32 centerXPixels = (minX + maxX) * 0.5f * pixelsPerMmX;
    const f32 centerYPixels = (minY + maxY) * 0.5f * pixelsPerMmY;
    return {
        static_cast<std::int64_t>(std::floor(centerXPixels / spacingPixels)),
        static_cast<std::int64_t>(std::floor(centerYPixels / spacingPixels)),
        orientation,
    };
}

} // namespace

ToolpathPreviewGeometry buildToolpathPreviewGeometry(const Toolpath& toolpath)
{
    ToolpathPreviewGeometry result;
    if (toolpath.points.size() < 2) return result;

    result.sourceMoveCount = toolpath.points.size() - 1;
    std::vector<Vec3> pendingRapid;
    std::vector<Vec3> currentCut;

    auto finishCutStroke = [&]() {
        if (currentCut.empty()) return;

        ToolpathPreviewStroke stroke;
        stroke.rapidPoints = simplifyTopViewPolyline(pendingRapid);
        stroke.cutPoints = simplifyTopViewPolyline(currentCut);
        if (stroke.cutPoints.size() >= 2) {
            result.strokes.push_back(std::move(stroke));
        }
        pendingRapid.clear();
        currentCut.clear();
    };

    Vec3 previous = toolpath.points.front().position;
    for (std::size_t index = 1; index < toolpath.points.size(); ++index) {
        const ToolpathPoint& current = toolpath.points[index];
        if (current.rapid) {
            finishCutStroke();
            if (pendingRapid.empty()) pendingRapid.push_back(previous);
            pendingRapid.push_back(current.position);
        } else {
            if (currentCut.empty()) currentCut.push_back(previous);
            currentCut.push_back(current.position);
        }
        previous = current.position;
    }
    finishCutStroke();
    return result;
}

MultiPassToolpathPreviewGeometry buildToolpathPreviewGeometry(
    const MultiPassToolpath& toolpath)
{
    return {
        buildToolpathPreviewGeometry(toolpath.clearing),
        buildToolpathPreviewGeometry(toolpath.finishing),
    };
}

ToolpathPreviewSelection selectToolpathPreviewStrokes(
    const ToolpathPreviewGeometry& geometry,
    f32 pixelsPerMmX,
    f32 pixelsPerMmY,
    f32 minimumSpacingPixels,
    std::size_t maximumStrokes)
{
    ToolpathPreviewSelection result;
    result.totalStrokeCount = geometry.strokes.size();
    if (geometry.strokes.empty() || maximumStrokes == 0) return result;

    const f32 scaleX = std::isfinite(pixelsPerMmX) && pixelsPerMmX > 0.0f
                           ? pixelsPerMmX
                           : 1.0f;
    const f32 scaleY = std::isfinite(pixelsPerMmY) && pixelsPerMmY > 0.0f
                           ? pixelsPerMmY
                           : 1.0f;
    const f32 spacing =
        std::isfinite(minimumSpacingPixels) && minimumSpacingPixels > 0.0f
            ? minimumSpacingPixels
            : 1.0f;

    std::unordered_set<DisplayCell, DisplayCellHash> occupied;
    occupied.reserve(geometry.strokes.size());
    std::vector<std::size_t> candidates;
    candidates.reserve(std::min(geometry.strokes.size(), maximumStrokes));

    for (std::size_t index = 0; index < geometry.strokes.size(); ++index) {
        const auto& stroke = geometry.strokes[index];
        if (stroke.cutPoints.size() < 2) continue;
        if (occupied.insert(displayCellFor(stroke, scaleX, scaleY, spacing)).second) {
            candidates.push_back(index);
        }
    }

    if (candidates.size() <= maximumStrokes) {
        result.strokeIndices = std::move(candidates);
        return result;
    }

    result.strokeIndices.reserve(maximumStrokes);
    if (maximumStrokes == 1) {
        result.strokeIndices.push_back(candidates.front());
        return result;
    }

    const f64 sourceSpan = static_cast<f64>(candidates.size() - 1);
    const f64 outputSpan = static_cast<f64>(maximumStrokes - 1);
    for (std::size_t outputIndex = 0; outputIndex < maximumStrokes; ++outputIndex) {
        const auto candidateIndex = static_cast<std::size_t>(
            std::llround(static_cast<f64>(outputIndex) * sourceSpan / outputSpan));
        result.strokeIndices.push_back(candidates[candidateIndex]);
    }
    return result;
}

} // namespace dw::carve
