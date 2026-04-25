#include "multi_stock_optimizer.h"

#include <algorithm>
#include <limits>
#include <map>

namespace dw {
namespace optimizer {

namespace {

int placedCount(const CutPlan& plan) {
    int count = 0;
    for (const auto& sheet : plan.sheets) {
        count += static_cast<int>(sheet.placements.size());
    }
    return count;
}

bool isBetterCandidate(const CutPlan& candidate,
                       f32 candidateCost,
                       const CutPlan& current,
                       f32 currentCost,
                       bool hasCurrent) {
    if (!hasCurrent) {
        return true;
    }

    if (candidate.isComplete() != current.isComplete()) {
        return candidate.isComplete();
    }

    if (!candidate.isComplete()) {
        int candidatePlaced = placedCount(candidate);
        int currentPlaced = placedCount(current);
        if (candidatePlaced != currentPlaced) {
            return candidatePlaced > currentPlaced;
        }
    }

    if (candidateCost != currentCost) {
        return candidateCost < currentCost;
    }

    if (candidate.sheetsUsed != current.sheetsUsed) {
        return candidate.sheetsUsed < current.sheetsUsed;
    }

    if (candidate.totalWasteArea != current.totalWasteArea) {
        return candidate.totalWasteArea < current.totalWasteArea;
    }

    return false;
}

void relinkPlacements(CutPlan& plan,
                      const std::vector<const Part*>& originalPartsByGroupIndex) {
    for (auto& sheet : plan.sheets) {
        for (auto& placement : sheet.placements) {
            if (placement.partIndex >= 0 &&
                placement.partIndex < static_cast<int>(originalPartsByGroupIndex.size())) {
                placement.part = originalPartsByGroupIndex[static_cast<usize>(placement.partIndex)];
            }
        }
    }
}

} // namespace

MultiStockResult optimizeMultiStock(
    const std::vector<Part>& parts,
    const std::vector<MaterialGroup>& materials,
    Algorithm algorithm,
    bool allowRotation, f32 kerf, f32 margin) {

    MultiStockResult result;

    // Build a map of materialId -> MaterialGroup for quick lookup
    std::map<i64, const MaterialGroup*> groupMap;
    for (const auto& mg : materials) {
        groupMap[mg.materialId] = &mg;
    }

    // Group parts by materialId
    std::map<i64, std::vector<Part>> partsByMaterial;
    std::map<i64, std::vector<const Part*>> originalPartsByMaterial;
    for (const auto& part : parts) {
        partsByMaterial[part.materialId].push_back(part);
        originalPartsByMaterial[part.materialId].push_back(&part);
    }

    // For each material group that has parts, run optimization
    for (auto& [matId, groupParts] : partsByMaterial) {
        // Find the MaterialGroup definition
        auto it = groupMap.find(matId);
        if (it == groupMap.end()) {
            // No material group defined for this materialId -- skip
            continue;
        }

        const MaterialGroup* mg = it->second;
        if (mg->stockSizes.empty()) {
            continue;
        }

        // Try each stock size and pick the one with minimum cost
        CutPlan bestPlan;
        Sheet bestSheet;
        f32 bestCost = std::numeric_limits<f32>::max();
        bool foundAny = false;

        for (const auto& stock : mg->stockSizes) {
            auto opt = CutOptimizer::create(algorithm);
            opt->setAllowRotation(allowRotation);
            opt->setKerf(kerf);
            opt->setMargin(margin);

            // Use unlimited quantity for the stock sheet
            Sheet s = stock;
            s.quantity = 0; // unlimited

            CutPlan plan = opt->optimize(groupParts, {s});

            // Calculate cost: sheetsUsed * sheet cost
            f32 cost = static_cast<f32>(plan.sheetsUsed) * stock.cost;
            plan.totalCost = cost;

            // Pick the best: complete first, then more placed parts, then cost.
            if (isBetterCandidate(plan, cost, bestPlan, bestCost, foundAny)) {
                bestPlan = plan;
                bestSheet = stock;
                bestCost = cost;
                foundAny = true;
            }
        }

        if (foundAny) {
            relinkPlacements(bestPlan, originalPartsByMaterial[matId]);

            MultiStockResult::GroupResult gr;
            gr.materialId = matId;
            gr.materialName = mg->materialName;
            gr.plan = bestPlan;
            gr.usedSheet = bestSheet;
            gr.waste = computeWasteBreakdown(bestPlan, bestSheet, kerf);
            result.groups.push_back(gr);

            result.totalCost += bestCost;
            result.totalSheetsUsed += bestPlan.sheetsUsed;
        }
    }

    return result;
}

} // namespace optimizer
} // namespace dw
