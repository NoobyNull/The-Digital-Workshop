#include <gtest/gtest.h>
#include "core/optimizer/multi_stock_optimizer.h"

using namespace dw;
using namespace dw::optimizer;

TEST(MultiStock, GroupsByMaterial) {
    // 3 parts: 2 with materialId=1, 1 with materialId=2
    std::vector<Part> parts;
    Part p1(400.0f, 300.0f, 1);
    p1.materialId = 1;
    p1.name = "Side A";
    parts.push_back(p1);

    Part p2(400.0f, 300.0f, 1);
    p2.materialId = 1;
    p2.name = "Side B";
    parts.push_back(p2);

    Part p3(200.0f, 200.0f, 1);
    p3.materialId = 2;
    p3.name = "Top";
    parts.push_back(p3);

    Sheet stock1(2440.0f, 1220.0f, 45.0f);
    stock1.name = "Plywood 4x8";
    Sheet stock2(2440.0f, 1220.0f, 60.0f);
    stock2.name = "MDF 4x8";

    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {stock1}},
        {2, "MDF", {}, {stock2}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 3.0f, 5.0f);

    // Should have 2 groups
    ASSERT_EQ(result.groups.size(), 2u);

    // Group for material 1 should have 2 parts placed
    bool foundMat1 = false, foundMat2 = false;
    for (const auto& g : result.groups) {
        if (g.materialId == 1) {
            foundMat1 = true;
            // 2 parts should be placed
            int placementCount = 0;
            for (const auto& sr : g.plan.sheets)
                placementCount += static_cast<int>(sr.placements.size());
            EXPECT_EQ(placementCount, 2);
        }
        if (g.materialId == 2) {
            foundMat2 = true;
            int placementCount = 0;
            for (const auto& sr : g.plan.sheets)
                placementCount += static_cast<int>(sr.placements.size());
            EXPECT_EQ(placementCount, 1);
        }
    }
    EXPECT_TRUE(foundMat1);
    EXPECT_TRUE(foundMat2);
}

TEST(MultiStock, PicksCheaperSheet) {
    // Single small part, two stock sizes -- should pick cheaper
    std::vector<Part> parts;
    Part p(200.0f, 200.0f, 1);
    p.materialId = 1;
    parts.push_back(p);

    Sheet expensive(2440.0f, 1220.0f, 50.0f);
    expensive.name = "Large expensive";
    Sheet cheap(1220.0f, 610.0f, 20.0f);
    cheap.name = "Small cheap";

    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {expensive, cheap}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 0.0f, 0.0f);

    ASSERT_EQ(result.groups.size(), 1u);
    // Should pick the $20 sheet since the part fits on both
    EXPECT_NEAR(result.groups[0].usedSheet.cost, 20.0f, 0.01f);
}

TEST(MultiStock, FallbackToLargerSheet) {
    // Part that only fits on the larger sheet
    std::vector<Part> parts;
    Part p(2000.0f, 1000.0f, 1);
    p.materialId = 1;
    parts.push_back(p);

    Sheet small(1220.0f, 610.0f, 20.0f);
    small.name = "Small";
    Sheet large(2440.0f, 1220.0f, 50.0f);
    large.name = "Large";

    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {small, large}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 0.0f, 0.0f);

    ASSERT_EQ(result.groups.size(), 1u);
    // Must use the large sheet since part doesn't fit on small
    EXPECT_NEAR(result.groups[0].usedSheet.width, 2440.0f, 0.01f);
    EXPECT_TRUE(result.groups[0].plan.isComplete());
}

TEST(MultiStock, UnassignedPartsGroupedSeparately) {
    // Parts with materialId=0 should go into a default group
    std::vector<Part> parts;
    Part p1(400.0f, 300.0f, 1);
    p1.materialId = 1;
    parts.push_back(p1);

    Part p2(200.0f, 200.0f, 1);
    p2.materialId = 0; // unassigned
    parts.push_back(p2);

    Sheet stock(2440.0f, 1220.0f, 45.0f);

    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {stock}},
        {0, "Unassigned", {}, {stock}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 0.0f, 0.0f);

    // Should have 2 groups: material 1 and default (0)
    ASSERT_EQ(result.groups.size(), 2u);

    bool hasDefault = false;
    for (const auto& g : result.groups) {
        if (g.materialId == 0) hasDefault = true;
    }
    EXPECT_TRUE(hasDefault);
}

TEST(MultiStock, TotalCostAggregated) {
    std::vector<Part> parts;
    Part p1(400.0f, 300.0f, 1);
    p1.materialId = 1;
    parts.push_back(p1);

    Part p2(300.0f, 200.0f, 1);
    p2.materialId = 2;
    parts.push_back(p2);

    Sheet stock1(2440.0f, 1220.0f, 45.0f);
    Sheet stock2(2440.0f, 1220.0f, 60.0f);

    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {stock1}},
        {2, "MDF", {}, {stock2}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 0.0f, 0.0f);

    // totalCost should be sum of group costs
    f32 sumCost = 0.0f;
    int sumSheets = 0;
    for (const auto& g : result.groups) {
        sumCost += g.plan.totalCost;
        sumSheets += g.plan.sheetsUsed;
    }
    EXPECT_NEAR(result.totalCost, sumCost, 0.01f);
    EXPECT_EQ(result.totalSheetsUsed, sumSheets);
    EXPECT_GT(result.totalCost, 0.0f);
    EXPECT_GT(result.totalSheetsUsed, 0);
}

TEST(MultiStock, PicksMoreCompletePlanBeforeCheapestIncompletePlan) {
    std::vector<Part> parts;
    Part p(2000.0f, 1000.0f, 1);
    p.materialId = 1;
    parts.push_back(p);

    Sheet cheapTooSmall(500.0f, 500.0f, 1.0f);
    cheapTooSmall.name = "Cheap too small";
    Sheet expensiveFits(2440.0f, 1220.0f, 50.0f);
    expensiveFits.name = "Expensive fits";

    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {cheapTooSmall, expensiveFits}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 0.0f, 0.0f);

    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_TRUE(result.groups[0].plan.isComplete());
    EXPECT_EQ(result.groups[0].usedSheet.name, "Expensive fits");
}

TEST(MultiStock, ReturnedPlacementsPointAtCallerParts) {
    std::vector<Part> parts;
    Part p(400.0f, 300.0f, 1);
    p.materialId = 1;
    p.name = "Stable source";
    parts.push_back(p);

    Sheet stock(2440.0f, 1220.0f, 45.0f);
    std::vector<MaterialGroup> materials = {
        {1, "Plywood", {}, {stock}},
    };

    MultiStockResult result = optimizeMultiStock(
        parts, materials, Algorithm::Guillotine, true, 0.0f, 0.0f);

    ASSERT_EQ(result.groups.size(), 1u);
    ASSERT_EQ(result.groups[0].plan.sheets.size(), 1u);
    ASSERT_EQ(result.groups[0].plan.sheets[0].placements.size(), 1u);
    EXPECT_EQ(result.groups[0].plan.sheets[0].placements[0].part, &parts[0]);
}
