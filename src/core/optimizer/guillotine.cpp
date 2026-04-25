#include "guillotine.h"

#include <algorithm>

#include "optimizer_utils.h"

namespace dw {
namespace optimizer {

namespace {
constexpr f32 PLACEMENT_EPSILON = 0.001f;

bool allPlaced(const std::vector<bool>& placed) {
    return std::all_of(placed.begin(), placed.end(), [](bool p) { return p; });
}
} // namespace

CutPlan GuillotineOptimizer::optimize(const std::vector<Part>& parts,
                                      const std::vector<Sheet>& sheets) {
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

            // Create root node for this sheet
            auto root = std::make_unique<Node>();
            root->x = m_margin;
            root->y = m_margin;
            root->width = effectiveWidth;
            root->height = effectiveHeight;

            SheetResult sheetResult;

            // Try to place each unplaced part
            for (usize i = 0; i < expandedParts.size(); ++i) {
                if (placed[i]) {
                    continue;
                }

                const auto& ep = expandedParts[i];
                f32 partWidth = ep.part->width + m_kerf;
                f32 partHeight = ep.part->height + m_kerf;

                // Try to insert (with optional rotation)
                Node* node = insert(root.get(), partWidth, partHeight);

                if (!node && m_allowRotation && ep.part->canRotate) {
                    node = insert(root.get(), partHeight, partWidth);
                    if (node) {
                        // Mark as rotated
                        Placement placement;
                        placement.part = ep.part;
                        placement.partIndex = ep.partIndex;
                        placement.instanceIndex = ep.instanceIndex;
                        placement.x = node->x;
                        placement.y = node->y;
                        placement.rotated = true;

                        sheetResult.placements.push_back(placement);
                        sheetResult.usedArea += ep.part->area();
                        placed[i] = true;
                        continue;
                    }
                }

                if (node) {
                    Placement placement;
                    placement.part = ep.part;
                    placement.partIndex = ep.partIndex;
                    placement.instanceIndex = ep.instanceIndex;
                    placement.x = node->x;
                    placement.y = node->y;
                    placement.rotated = false;

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
            unplaced.quantity = 1;
            plan.unplacedParts.push_back(unplaced);
        }
    }

    return plan;
}

GuillotineOptimizer::Node* GuillotineOptimizer::insert(Node* node, f32 width, f32 height) {
    // If this node is used, try children
    if (node->used) {
        Node* result = nullptr;
        if (node->right) {
            result = insert(node->right.get(), width, height);
        }
        if (!result && node->down) {
            result = insert(node->down.get(), width, height);
        }
        return result;
    }

    // If it doesn't fit, return null (with epsilon tolerance for float rounding)
    if (width > node->width + PLACEMENT_EPSILON || height > node->height + PLACEMENT_EPSILON) {
        return nullptr;
    }

    // Perfect fit (within epsilon)
    if (node->width - width < PLACEMENT_EPSILON && node->height - height < PLACEMENT_EPSILON) {
        node->used = true;
        return node;
    }

    // Split the node
    node->used = true;

    // Decide split direction (split along longer remainder)
    f32 dw = node->width - width;
    f32 dh = node->height - height;

    if (dw > dh) {
        // Horizontal split: right gets the wider piece
        node->right = std::make_unique<Node>();
        node->right->x = node->x + width;
        node->right->y = node->y;
        node->right->width = dw;
        node->right->height = height;

        node->down = std::make_unique<Node>();
        node->down->x = node->x;
        node->down->y = node->y + height;
        node->down->width = node->width;
        node->down->height = dh;
    } else {
        // Vertical split: down gets the taller piece
        node->right = std::make_unique<Node>();
        node->right->x = node->x + width;
        node->right->y = node->y;
        node->right->width = dw;
        node->right->height = node->height;

        node->down = std::make_unique<Node>();
        node->down->x = node->x;
        node->down->y = node->y + height;
        node->down->width = width;
        node->down->height = dh;
    }

    return node;
}

} // namespace optimizer
} // namespace dw
