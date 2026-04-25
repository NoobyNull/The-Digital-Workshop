// Digital Workshop - Cut Optimizer Tests

#include <gtest/gtest.h>

#include "core/optimizer/bin_packer.h"
#include "core/optimizer/cut_optimizer.h"
#include "core/optimizer/guillotine.h"
#include "core/optimizer/sheet.h"

using namespace dw::optimizer;

TEST(BinPacker, SinglePartFitsOnSheet) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    std::vector<Part> parts = {Part(100.0f, 50.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_TRUE(plan.unplacedParts.empty());
    ASSERT_EQ(plan.sheets.size(), 1u);
    ASSERT_EQ(plan.sheets[0].placements.size(), 1u);
    EXPECT_EQ(plan.sheetsUsed, 1);
}

TEST(BinPacker, MultiplePartsPlaced) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    std::vector<Part> parts = {
        Part(50.0f, 50.0f, 1),
        Part(50.0f, 50.0f, 1),
        Part(50.0f, 50.0f, 1),
    };
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_TRUE(plan.unplacedParts.empty());
    ASSERT_EQ(plan.sheets.size(), 1u);
    EXPECT_EQ(plan.sheets[0].placements.size(), 3u);
}

TEST(BinPacker, PartTooLargeForSheet) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);
    packer.setAllowRotation(true);

    // Part is larger than sheet in both orientations
    std::vector<Part> parts = {Part(300.0f, 300.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);

    EXPECT_FALSE(plan.isComplete());
    EXPECT_EQ(plan.unplacedParts.size(), 1u);
    EXPECT_EQ(plan.sheetsUsed, 0);
}

TEST(BinPacker, EmptyPartsReturnsEmptyPlan) {
    BinPacker packer;

    std::vector<Part> parts;
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheetsUsed, 0);
    EXPECT_TRUE(plan.sheets.empty());
}

TEST(BinPacker, EmptySheetsReturnsEmptyPlan) {
    BinPacker packer;

    std::vector<Part> parts = {Part(50.0f, 50.0f, 1)};
    std::vector<Sheet> sheets;

    CutPlan plan = packer.optimize(parts, sheets);

    // Parts exist but no sheets, so they are unplaced
    EXPECT_EQ(plan.sheetsUsed, 0);
    EXPECT_TRUE(plan.sheets.empty());
    EXPECT_FALSE(plan.isComplete());
    EXPECT_EQ(plan.unplacedParts.size(), 1u);
}

TEST(BinPacker, PartQuantityExpanded) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    // 1 part type with quantity 4
    std::vector<Part> parts = {Part(50.0f, 50.0f, 4)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    ASSERT_EQ(plan.sheets.size(), 1u);
    EXPECT_EQ(plan.sheets[0].placements.size(), 4u);
}

TEST(BinPacker, KerfReducesAvailableSpace) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    // Exactly fills the sheet without kerf
    std::vector<Part> parts = {
        Part(100.0f, 100.0f, 1),
        Part(100.0f, 100.0f, 1),
    };
    std::vector<Sheet> sheets = {Sheet(200.0f, 100.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());

    // Now with kerf, one part may not fit
    BinPacker packerWithKerf;
    packerWithKerf.setKerf(10.0f);
    packerWithKerf.setMargin(0.0f);

    CutPlan planKerf = packerWithKerf.optimize(parts, sheets);
    // With 10mm kerf, each part uses 110x110 effectively, so only one fits in 200x100
    EXPECT_FALSE(planKerf.isComplete());
}

TEST(BinPacker, MarginReducesAvailableSpace) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(50.0f);

    // Sheet is 200x200 but margin of 50 on each side leaves only 100x100 effective
    std::vector<Part> parts = {Part(100.0f, 100.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());

    // A part slightly larger than the effective area should not fit
    std::vector<Part> largeParts = {Part(101.0f, 101.0f, 1)};
    CutPlan plan2 = packer.optimize(largeParts, sheets);
    EXPECT_FALSE(plan2.isComplete());
}

TEST(BinPacker, NonZeroEfficiency) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    std::vector<Part> parts = {
        Part(200.0f, 100.0f, 1),
        Part(150.0f, 80.0f, 1),
        Part(100.0f, 100.0f, 1),
    };
    std::vector<Sheet> sheets = {Sheet(1000.0f, 500.0f)};

    CutPlan plan = packer.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_GT(plan.overallEfficiency(), 0.0f);
    EXPECT_LE(plan.overallEfficiency(), 1.0f);
    EXPECT_GT(plan.totalUsedArea, 0.0f);
}

TEST(CutOptimizer, FactoryCreatesBinPacker) {
    auto optimizer = CutOptimizer::create(Algorithm::FirstFitDecreasing);
    ASSERT_NE(optimizer, nullptr);

    // Verify it works
    std::vector<Part> parts = {Part(10.0f, 10.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(100.0f, 100.0f)};
    CutPlan plan = optimizer->optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
}

TEST(CutOptimizer, FactoryCreatesGuillotine) {
    auto optimizer = CutOptimizer::create(Algorithm::Guillotine);
    ASSERT_NE(optimizer, nullptr);

    std::vector<Part> parts = {Part(10.0f, 10.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(100.0f, 100.0f)};
    CutPlan plan = optimizer->optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
}

// --- TEST-03: Edge case tests ---

TEST(BinPacker, VerySmallParts) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    std::vector<Part> parts = {
        Part(0.1f, 0.1f, 1),
        Part(0.5f, 0.5f, 1),
        Part(1.0f, 0.01f, 1),
    };
    std::vector<Sheet> sheets = {Sheet(100.0f, 100.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheets[0].placements.size(), 3u);
}

TEST(BinPacker, ExactFit_NoWaste) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    // Part exactly equals sheet size
    std::vector<Part> parts = {Part(200.0f, 200.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
    ASSERT_EQ(plan.sheets[0].placements.size(), 1u);
}

TEST(BinPacker, ZeroKerfZeroMargin_MaxPacking) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);

    // 4 parts that should tile exactly in a 100x100 sheet
    std::vector<Part> parts = {
        Part(50.0f, 50.0f, 4),
    };
    std::vector<Sheet> sheets = {Sheet(100.0f, 100.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheets[0].placements.size(), 4u);
}

TEST(BinPacker, ManySmallParts_StressTest) {
    BinPacker packer;
    packer.setKerf(1.0f);
    packer.setMargin(5.0f);

    // 50 small parts on a large sheet
    std::vector<Part> parts;
    for (int i = 0; i < 50; ++i) {
        parts.emplace_back(10.0f, 10.0f, 1);
    }
    std::vector<Sheet> sheets = {Sheet(1000.0f, 1000.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
    EXPECT_GT(plan.overallEfficiency(), 0.0f);
}

TEST(BinPacker, RotationAllowsFit) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);
    packer.setAllowRotation(true);

    // Part is 150x50, sheet is 100x200 — only fits rotated (50x150)
    std::vector<Part> parts = {Part(150.0f, 50.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(100.0f, 200.0f)};

    CutPlan plan = packer.optimize(parts, sheets);
    EXPECT_TRUE(plan.isComplete());
}

TEST(BinPacker, UnlimitedStockCanUseMultipleSheets) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);
    packer.setAllowRotation(false);

    std::vector<Part> parts = {Part(100.0f, 100.0f, 2)};
    Sheet sheet(100.0f, 100.0f, 10.0f);
    sheet.quantity = 0; // unlimited

    CutPlan plan = packer.optimize(parts, {sheet});

    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheetsUsed, 2);
    EXPECT_NEAR(plan.totalCost, 20.0f, 0.01f);
}

TEST(BinPacker, FiniteStockQuantityLeavesOverflowUnplaced) {
    BinPacker packer;
    packer.setKerf(0.0f);
    packer.setMargin(0.0f);
    packer.setAllowRotation(false);

    std::vector<Part> parts = {Part(100.0f, 100.0f, 2)};
    Sheet sheet(100.0f, 100.0f, 10.0f);
    sheet.quantity = 1;

    CutPlan plan = packer.optimize(parts, {sheet});

    EXPECT_FALSE(plan.isComplete());
    EXPECT_EQ(plan.sheetsUsed, 1);
    EXPECT_EQ(plan.unplacedParts.size(), 1u);
}

// --- GuillotineOptimizer tests ---

TEST(Guillotine, SinglePartFitsOnSheet) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);

    std::vector<Part> parts = {Part(100.0f, 50.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = guillotine.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_TRUE(plan.unplacedParts.empty());
    ASSERT_EQ(plan.sheets.size(), 1u);
    ASSERT_EQ(plan.sheets[0].placements.size(), 1u);
}

TEST(Guillotine, MultiplePartsPlaced) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);

    std::vector<Part> parts = {
        Part(50.0f, 50.0f, 1),
        Part(50.0f, 50.0f, 1),
        Part(50.0f, 50.0f, 1),
    };
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = guillotine.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheets[0].placements.size(), 3u);
}

TEST(Guillotine, PartTooLarge) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);
    guillotine.setAllowRotation(true);

    std::vector<Part> parts = {Part(300.0f, 300.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = guillotine.optimize(parts, sheets);

    EXPECT_FALSE(plan.isComplete());
    EXPECT_EQ(plan.unplacedParts.size(), 1u);
}

TEST(Guillotine, EmptyPartsReturnsEmptyPlan) {
    GuillotineOptimizer guillotine;

    std::vector<Part> parts;
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = guillotine.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheetsUsed, 0);
}

TEST(Guillotine, KerfReducesSpace) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);

    // Two 100x100 parts fit in 200x100 without kerf
    std::vector<Part> parts = {Part(100.0f, 100.0f, 1), Part(100.0f, 100.0f, 1)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 100.0f)};

    CutPlan planNoKerf = guillotine.optimize(parts, sheets);
    EXPECT_TRUE(planNoKerf.isComplete());

    // With large kerf, second part may not fit
    GuillotineOptimizer guillotineKerf;
    guillotineKerf.setKerf(10.0f);
    guillotineKerf.setMargin(0.0f);

    CutPlan planKerf = guillotineKerf.optimize(parts, sheets);
    EXPECT_FALSE(planKerf.isComplete());
}

TEST(Guillotine, QuantityExpansion) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);

    std::vector<Part> parts = {Part(50.0f, 50.0f, 4)};
    std::vector<Sheet> sheets = {Sheet(200.0f, 200.0f)};

    CutPlan plan = guillotine.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheets[0].placements.size(), 4u);
}

TEST(Guillotine, NonZeroEfficiency) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);

    std::vector<Part> parts = {
        Part(200.0f, 100.0f, 1),
        Part(150.0f, 80.0f, 1),
    };
    std::vector<Sheet> sheets = {Sheet(500.0f, 500.0f)};

    CutPlan plan = guillotine.optimize(parts, sheets);

    EXPECT_TRUE(plan.isComplete());
    EXPECT_GT(plan.overallEfficiency(), 0.0f);
    EXPECT_LE(plan.overallEfficiency(), 1.0f);
}

TEST(Guillotine, UnlimitedStockCanUseMultipleSheets) {
    GuillotineOptimizer guillotine;
    guillotine.setKerf(0.0f);
    guillotine.setMargin(0.0f);
    guillotine.setAllowRotation(false);

    std::vector<Part> parts = {Part(100.0f, 100.0f, 2)};
    Sheet sheet(100.0f, 100.0f, 10.0f);
    sheet.quantity = 0; // unlimited

    CutPlan plan = guillotine.optimize(parts, {sheet});

    EXPECT_TRUE(plan.isComplete());
    EXPECT_EQ(plan.sheetsUsed, 2);
    EXPECT_NEAR(plan.totalCost, 20.0f, 0.01f);
}
