#include "bin_packer.h"

#include <algorithm>
#include <limits>

#include "optimizer_utils.h"

namespace dw {
namespace optimizer {

namespace {
constexpr f32 PLACEMENT_EPSILON = 0.001f;

bool allPlaced(const std::vector<bool>& placed) {
    return std::all_of(placed.begin(), placed.end(), [](bool p) { return p; });
}
} // namespace

CutPlan BinPacker::optimize(const std::vector<Part>& parts, const std::vector<Sheet>& sheets) {
    CutPlan plan;

    if (parts.empty()) {
        return plan;
    }

    // Expand parts by quantity and sort by area (decreasing)
    auto expandedParts = expandParts(parts);
    if (expandedParts.empty()) {
        return plan;
    }

    // Track which parts have been placed
    std::vector<bool> placed(expandedParts.size(), false);

    // Process sheets
    int emittedSheetIndex = 0;
    for (const Sheet& sheet : sheets) {
        // quantity == 0 means unlimited. One part per sheet is the hard upper
        // bound needed to prove completion for rectangular nesting.
        int repeatCount = sheet.quantity > 0
                              ? sheet.quantity
                              : static_cast<int>(expandedParts.size());

        for (int sheetCopy = 0; sheetCopy < repeatCount; ++sheetCopy) {
            if (allPlaced(placed)) {
                break;
            }

            // Effective sheet size after margins
            f32 effectiveWidth = sheet.width - 2 * m_margin;
            f32 effectiveHeight = sheet.height - 2 * m_margin;

            if (effectiveWidth <= 0 || effectiveHeight <= 0) {
                continue;
            }

            // Start with one free rectangle covering the whole sheet
            std::vector<FreeRect> freeRects;
            freeRects.push_back({m_margin, m_margin, effectiveWidth, effectiveHeight});

            SheetResult sheetResult;

            // Try to place each unplaced part
            for (usize i = 0; i < expandedParts.size(); ++i) {
                if (placed[i]) {
                    continue;
                }

                const auto& ep = expandedParts[i];
                Placement placement;
                placement.part = ep.part;
                placement.partIndex = ep.partIndex;
                placement.instanceIndex = ep.instanceIndex;

                if (tryPlace(*ep.part, ep.partIndex, ep.instanceIndex, freeRects, placement)) {
                    sheetResult.placements.push_back(placement);
                    sheetResult.usedArea += ep.part->area();
                    placed[i] = true;
                }
            }

            // Only add sheet if it has placements
            if (!sheetResult.placements.empty()) {
                sheetResult.sheetIndex = emittedSheetIndex++;
                sheetResult.wasteArea = sheet.area() - sheetResult.usedArea;
                plan.sheets.push_back(sheetResult);
                plan.totalUsedArea += sheetResult.usedArea;
                plan.totalWasteArea += sheetResult.wasteArea;
                plan.totalCost += sheet.cost;
                plan.sheetsUsed++;
            }
        }

        if (allPlaced(placed)) {
            break;
        }
    }

    // Collect unplaced parts
    for (usize i = 0; i < expandedParts.size(); ++i) {
        if (!placed[i]) {
            const auto& ep = expandedParts[i];
            Part unplaced = *ep.part;
            unplaced.quantity = 1; // Each instance is separate
            plan.unplacedParts.push_back(unplaced);
        }
    }

    return plan;
}

bool BinPacker::tryPlace(const Part& part,
                         int partIndex,
                         int instanceIndex,
                         std::vector<FreeRect>& freeRects,
                         Placement& outPlacement) {
    f32 partWidth = part.width + m_kerf;
    f32 partHeight = part.height + m_kerf;

    // Find best-fitting free rectangle
    usize bestRect = std::numeric_limits<usize>::max();
    bool bestRotated = false;
    f32 bestScore = std::numeric_limits<f32>::max();

    for (usize i = 0; i < freeRects.size(); ++i) {
        const FreeRect& rect = freeRects[i];

        // Try normal orientation (with epsilon tolerance for float rounding)
        if (partWidth <= rect.width + PLACEMENT_EPSILON &&
            partHeight <= rect.height + PLACEMENT_EPSILON) {
            f32 score = rect.width * rect.height - partWidth * partHeight;
            if (score < bestScore) {
                bestScore = score;
                bestRect = i;
                bestRotated = false;
            }
        }

        // Try rotated orientation (respects per-part canRotate for grain direction)
        if (m_allowRotation && part.canRotate &&
            partHeight <= rect.width + PLACEMENT_EPSILON &&
            partWidth <= rect.height + PLACEMENT_EPSILON) {
            f32 score = rect.width * rect.height - partHeight * partWidth;
            if (score < bestScore) {
                bestScore = score;
                bestRect = i;
                bestRotated = true;
            }
        }
    }

    if (bestRect == std::numeric_limits<usize>::max()) {
        return false;
    }

    // Place the part
    outPlacement.partIndex = partIndex;
    outPlacement.instanceIndex = instanceIndex;
    outPlacement.x = freeRects[bestRect].x;
    outPlacement.y = freeRects[bestRect].y;
    outPlacement.rotated = bestRotated;

    // Split the free rectangle
    splitFreeRect(freeRects, bestRect, outPlacement);

    return true;
}

void BinPacker::splitFreeRect(std::vector<FreeRect>& freeRects,
                              usize rectIndex,
                              const Placement& placement) {
    FreeRect rect = freeRects[rectIndex];

    f32 placedWidth = placement.getWidth() + m_kerf;
    f32 placedHeight = placement.getHeight() + m_kerf;

    // Remove the used rectangle
    freeRects.erase(freeRects.begin() + static_cast<long>(rectIndex));

    // Create new free rectangles from remaining space
    // Right remainder
    if (rect.width - placedWidth > 0) {
        FreeRect right;
        right.x = rect.x + placedWidth;
        right.y = rect.y;
        right.width = rect.width - placedWidth;
        right.height = rect.height;
        freeRects.push_back(right);
    }

    // Top remainder
    if (rect.height - placedHeight > 0) {
        FreeRect top;
        top.x = rect.x;
        top.y = rect.y + placedHeight;
        top.width = placedWidth;
        top.height = rect.height - placedHeight;
        freeRects.push_back(top);
    }
}

} // namespace optimizer
} // namespace dw
