#include "waste_breakdown.h"

#include <algorithm>
#include <cmath>

namespace dw {
namespace optimizer {

WasteBreakdown computeWasteBreakdown(const CutPlan& plan,
                                      const Sheet& sheetTemplate,
                                      f32 kerf) {
    WasteBreakdown wb;

    if (plan.sheets.empty()) {
        return wb;
    }

    const f32 sheetArea = sheetTemplate.area();
    if (sheetArea <= 0.0f) {
        return wb;
    }

    f32 totalKerfArea = 0.0f;
    f32 totalScrapArea = 0.0f;

    for (size_t si = 0; si < plan.sheets.size(); ++si) {
        const SheetResult& sr = plan.sheets[si];
        int sheetIndex = static_cast<int>(si);

        // Compute bounding box of all placements on this sheet
        f32 maxRight = 0.0f;
        f32 maxBottom = 0.0f;
        f32 sheetKerfArea = 0.0f;

        for (const auto& pl : sr.placements) {
            f32 pw = pl.getWidth();
            f32 ph = pl.getHeight();

            f32 right = pl.x + pw;
            f32 bottom = pl.y + ph;
            maxRight = std::max(maxRight, right);
            maxBottom = std::max(maxBottom, bottom);

            // Kerf area per placement: kerf strip along right and bottom edges
            if (kerf > 0.0f) {
                sheetKerfArea += kerf * (pw + ph);
            }
        }

        totalKerfArea += sheetKerfArea;

        // Compute scrap rectangles from the bounding box of placed parts:
        // Right strip: from maxRight to sheetWidth, full sheet height
        // Bottom strip: from maxBottom to sheetHeight, up to maxRight width (avoid overlap)
        f32 rightW = sheetTemplate.width - maxRight;
        f32 bottomH = sheetTemplate.height - maxBottom;

        if (rightW > 0.0f && sheetTemplate.height > 0.0f) {
            ScrapPiece sp;
            sp.x = maxRight;
            sp.y = 0.0f;
            sp.width = rightW;
            sp.height = sheetTemplate.height;
            sp.sheetIndex = sheetIndex;
            wb.scrapPieces.push_back(sp);
            totalScrapArea += sp.area();
        }

        if (bottomH > 0.0f && maxRight > 0.0f) {
            ScrapPiece sp;
            sp.x = 0.0f;
            sp.y = maxBottom;
            sp.width = maxRight;
            sp.height = bottomH;
            sp.sheetIndex = sheetIndex;
            wb.scrapPieces.push_back(sp);
            totalScrapArea += sp.area();
        }
    }

    wb.totalScrapArea = totalScrapArea;
    wb.totalKerfArea = totalKerfArea;

    // Unusable waste = total waste - scrap - kerf (clamped to 0)
    f32 totalWaste = plan.totalWasteArea;
    wb.totalUnusableArea = std::max(0.0f, totalWaste - totalScrapArea - totalKerfArea);

    // Dollar values proportional to area ratios
    f32 totalSheetArea = sheetArea * static_cast<f32>(plan.sheetsUsed);
    f32 totalCost = sheetTemplate.cost * static_cast<f32>(plan.sheetsUsed);

    if (totalSheetArea > 0.0f && totalCost > 0.0f) {
        f32 rate = totalCost / totalSheetArea;
        wb.scrapValue = wb.totalScrapArea * rate;
        wb.kerfValue = wb.totalKerfArea * rate;
        wb.unusableValue = wb.totalUnusableArea * rate;
        wb.totalWasteValue = wb.scrapValue + wb.kerfValue + wb.unusableValue;
    }

    return wb;
}

} // namespace optimizer
} // namespace dw
