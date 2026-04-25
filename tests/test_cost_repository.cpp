// Digital Workshop - Cost Repository Tests

#include <cmath>

#include <gtest/gtest.h>

#include "core/database/cost_repository.h"
#include "core/database/database.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"

namespace {

// Fixture: in-memory DB with schema initialized
class CostRepoTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
        m_repo = std::make_unique<dw::CostRepository>(m_db);
        m_projectRepo = std::make_unique<dw::ProjectRepository>(m_db);
    }

    // Create a project record and return its ID
    dw::i64 createProject(const std::string& name) {
        dw::ProjectRecord p;
        p.name = name;
        auto id = m_projectRepo->insert(p);
        return id.value_or(0);
    }

    dw::CostingRecord makeRecord(const std::string& name, dw::i64 projectId = 0) {
        dw::CostingRecord est;
        est.name = name;
        est.projectId = projectId;
        est.notes = "Test record";
        return est;
    }

    dw::CostItem makeItem(const std::string& name,
                          dw::CostCategory category,
                          dw::f64 quantity,
                          dw::f64 rate) {
        dw::CostItem item;
        item.name = name;
        item.category = category;
        item.quantity = quantity;
        item.rate = rate;
        item.total = quantity * rate;
        return item;
    }

    dw::Database m_db;
    std::unique_ptr<dw::CostRepository> m_repo;
    std::unique_ptr<dw::ProjectRepository> m_projectRepo;
};

} // namespace

// --- Insert ---

TEST_F(CostRepoTest, Insert_ReturnsId) {
    auto est = makeRecord("Basic Estimate");
    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());
    EXPECT_GT(id.value(), 0);
}

TEST_F(CostRepoTest, Insert_WithItems) {
    auto est = makeRecord("Estimate With Items");
    est.items.push_back(makeItem("Plywood", dw::CostCategory::Material, 3.0, 25.50));
    est.items.push_back(makeItem("Assembly", dw::CostCategory::Labor, 2.0, 45.00));
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->items.size(), 2u);
    EXPECT_EQ(found->items[0].name, "Plywood");
    EXPECT_EQ(found->items[1].name, "Assembly");
}

TEST_F(CostRepoTest, Insert_WithProjectId) {
    dw::i64 pid = createProject("Test Project");
    ASSERT_GT(pid, 0);
    auto est = makeRecord("Project Estimate", pid);
    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->projectId, pid);
}

TEST_F(CostRepoTest, Insert_WithoutProjectId) {
    auto est = makeRecord("Standalone Estimate");
    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->projectId, 0);
}

// --- FindById ---

TEST_F(CostRepoTest, FindById_Found) {
    auto est = makeRecord("Findable");
    est.items.push_back(makeItem("Wood", dw::CostCategory::Material, 1.0, 10.0));
    est.taxRate = 8.0;
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Findable");
    EXPECT_DOUBLE_EQ(found->subtotal, 10.0);
    EXPECT_DOUBLE_EQ(found->taxRate, 8.0);
    EXPECT_DOUBLE_EQ(found->taxAmount, 0.8);
    EXPECT_DOUBLE_EQ(found->total, 10.8);
    EXPECT_EQ(found->notes, "Test record");
}

TEST_F(CostRepoTest, FindById_NotFound) {
    auto found = m_repo->findById(999);
    EXPECT_FALSE(found.has_value());
}

// --- FindAll ---

TEST_F(CostRepoTest, FindAll_Empty) {
    auto all = m_repo->findAll();
    EXPECT_TRUE(all.empty());
}

TEST_F(CostRepoTest, FindAll_Multiple) {
    m_repo->insert(makeRecord("Estimate A"));
    m_repo->insert(makeRecord("Estimate B"));
    m_repo->insert(makeRecord("Estimate C"));

    auto all = m_repo->findAll();
    EXPECT_EQ(all.size(), 3u);
}

// --- FindByProject ---

TEST_F(CostRepoTest, FindByProject_MatchesCorrectProject) {
    dw::i64 pid1 = createProject("Project 1");
    dw::i64 pid2 = createProject("Project 2");
    m_repo->insert(makeRecord("Project 1 Est A", pid1));
    m_repo->insert(makeRecord("Project 1 Est B", pid1));
    m_repo->insert(makeRecord("Project 2 Est A", pid2));
    m_repo->insert(makeRecord("Standalone"));

    auto proj1 = m_repo->findByProject(pid1);
    EXPECT_EQ(proj1.size(), 2u);

    auto proj2 = m_repo->findByProject(pid2);
    EXPECT_EQ(proj2.size(), 1u);
    EXPECT_EQ(proj2[0].name, "Project 2 Est A");
}

TEST_F(CostRepoTest, FindByProject_NoMatch) {
    dw::i64 pid = createProject("Some Project");
    m_repo->insert(makeRecord("Some Estimate", pid));
    auto results = m_repo->findByProject(99);
    EXPECT_TRUE(results.empty());
}

// --- Update ---

TEST_F(CostRepoTest, Update_ChangesName) {
    auto est = makeRecord("Original Name");
    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    found->name = "Updated Name";
    EXPECT_TRUE(m_repo->update(*found));

    auto updated = m_repo->findById(id.value());
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->name, "Updated Name");
}

TEST_F(CostRepoTest, Update_ChangesItems) {
    auto est = makeRecord("Item Test");
    est.items.push_back(makeItem("Initial Item", dw::CostCategory::Material, 1.0, 5.0));
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    found->items.clear();
    found->items.push_back(makeItem("Replaced Item", dw::CostCategory::Labor, 3.0, 20.0));
    found->recalculate();
    EXPECT_TRUE(m_repo->update(*found));

    auto updated = m_repo->findById(id.value());
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->items.size(), 1u);
    EXPECT_EQ(updated->items[0].name, "Replaced Item");
    EXPECT_EQ(updated->items[0].category, dw::CostCategory::Labor);
    EXPECT_DOUBLE_EQ(updated->subtotal, 60.0);
}

TEST_F(CostRepoTest, Update_ChangesFinancials) {
    auto est = makeRecord("Financial Test");
    est.items.push_back(makeItem("Part", dw::CostCategory::Material, 10.0, 5.0));
    est.taxRate = 10.0;
    est.discountRate = 5.0;
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    found->taxRate = 15.0;
    found->discountRate = 0.0;
    found->recalculate();
    EXPECT_TRUE(m_repo->update(*found));

    auto updated = m_repo->findById(id.value());
    ASSERT_TRUE(updated.has_value());
    EXPECT_DOUBLE_EQ(updated->taxRate, 15.0);
    EXPECT_DOUBLE_EQ(updated->discountRate, 0.0);
    EXPECT_DOUBLE_EQ(updated->subtotal, 50.0);
    EXPECT_DOUBLE_EQ(updated->taxAmount, 7.5);
    EXPECT_DOUBLE_EQ(updated->discountAmount, 0.0);
    EXPECT_DOUBLE_EQ(updated->total, 57.5);
}

// --- Remove ---

TEST_F(CostRepoTest, Remove_ById) {
    auto id = m_repo->insert(makeRecord("To Remove"));
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(m_repo->count(), 1);

    EXPECT_TRUE(m_repo->remove(id.value()));
    EXPECT_EQ(m_repo->count(), 0);
}

TEST_F(CostRepoTest, Remove_NonexistentReturnsTrueButNoEffect) {
    m_repo->insert(makeRecord("Keeper"));
    EXPECT_EQ(m_repo->count(), 1);

    // Removing a non-existent ID executes successfully (no row matched)
    m_repo->remove(999);
    EXPECT_EQ(m_repo->count(), 1);
}

// --- Count ---

TEST_F(CostRepoTest, Count_Empty) {
    EXPECT_EQ(m_repo->count(), 0);
}

TEST_F(CostRepoTest, Count_AfterInserts) {
    EXPECT_EQ(m_repo->count(), 0);
    m_repo->insert(makeRecord("A"));
    EXPECT_EQ(m_repo->count(), 1);
    m_repo->insert(makeRecord("B"));
    EXPECT_EQ(m_repo->count(), 2);
}

TEST_F(CostRepoTest, Count_AfterRemove) {
    auto id = m_repo->insert(makeRecord("A"));
    m_repo->insert(makeRecord("B"));
    EXPECT_EQ(m_repo->count(), 2);

    m_repo->remove(id.value());
    EXPECT_EQ(m_repo->count(), 1);
}

// --- CostingRecord::recalculate ---

TEST_F(CostRepoTest, Recalculate_EmptyItems) {
    dw::CostingRecord est;
    est.taxRate = 10.0;
    est.discountRate = 5.0;
    est.recalculate();

    EXPECT_DOUBLE_EQ(est.subtotal, 0.0);
    EXPECT_DOUBLE_EQ(est.taxAmount, 0.0);
    EXPECT_DOUBLE_EQ(est.discountAmount, 0.0);
    EXPECT_DOUBLE_EQ(est.total, 0.0);
}

TEST_F(CostRepoTest, Recalculate_SingleItem) {
    dw::CostingRecord est;
    est.items.push_back(makeItem("Widget", dw::CostCategory::Material, 4.0, 12.50));
    est.recalculate();

    EXPECT_DOUBLE_EQ(est.items[0].total, 50.0);
    EXPECT_DOUBLE_EQ(est.subtotal, 50.0);
    EXPECT_DOUBLE_EQ(est.total, 50.0);
}

TEST_F(CostRepoTest, Recalculate_MultipleItems) {
    dw::CostingRecord est;
    est.items.push_back(makeItem("Material A", dw::CostCategory::Material, 2.0, 10.0));
    est.items.push_back(makeItem("Labor B", dw::CostCategory::Labor, 3.0, 20.0));
    est.items.push_back(makeItem("Tool C", dw::CostCategory::Tool, 1.0, 15.0));
    est.recalculate();

    EXPECT_DOUBLE_EQ(est.items[0].total, 20.0);
    EXPECT_DOUBLE_EQ(est.items[1].total, 60.0);
    EXPECT_DOUBLE_EQ(est.items[2].total, 15.0);
    EXPECT_DOUBLE_EQ(est.subtotal, 95.0);
    EXPECT_DOUBLE_EQ(est.total, 95.0);
}

TEST_F(CostRepoTest, Recalculate_WithTax) {
    dw::CostingRecord est;
    est.items.push_back(makeItem("Item", dw::CostCategory::Material, 1.0, 100.0));
    est.taxRate = 8.5;
    est.recalculate();

    EXPECT_DOUBLE_EQ(est.subtotal, 100.0);
    EXPECT_DOUBLE_EQ(est.taxAmount, 8.5);
    EXPECT_DOUBLE_EQ(est.total, 108.5);
}

TEST_F(CostRepoTest, Recalculate_WithDiscount) {
    dw::CostingRecord est;
    est.items.push_back(makeItem("Item", dw::CostCategory::Material, 1.0, 200.0));
    est.discountRate = 10.0;
    est.recalculate();

    EXPECT_DOUBLE_EQ(est.subtotal, 200.0);
    EXPECT_DOUBLE_EQ(est.discountAmount, 20.0);
    EXPECT_DOUBLE_EQ(est.total, 180.0);
}

TEST_F(CostRepoTest, Recalculate_WithTaxAndDiscount) {
    dw::CostingRecord est;
    est.items.push_back(makeItem("Item", dw::CostCategory::Material, 5.0, 20.0));
    est.taxRate = 10.0;
    est.discountRate = 5.0;
    est.recalculate();

    // subtotal = 100.0, tax = 10.0, discount = 5.0, total = 105.0
    EXPECT_DOUBLE_EQ(est.subtotal, 100.0);
    EXPECT_DOUBLE_EQ(est.taxAmount, 10.0);
    EXPECT_DOUBLE_EQ(est.discountAmount, 5.0);
    EXPECT_DOUBLE_EQ(est.total, 105.0);
}

TEST_F(CostRepoTest, Recalculate_UpdatesItemTotals) {
    dw::CostingRecord est;
    dw::CostItem item;
    item.name = "Manual";
    item.quantity = 3.0;
    item.rate = 7.0;
    item.total = 0.0; // Intentionally wrong
    est.items.push_back(item);
    est.recalculate();

    EXPECT_DOUBLE_EQ(est.items[0].total, 21.0);
    EXPECT_DOUBLE_EQ(est.subtotal, 21.0);
}

// --- Items serialization/deserialization ---

TEST_F(CostRepoTest, ItemsSerialization_EmptyItems) {
    auto est = makeRecord("Empty Items");
    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->items.empty());
}

TEST_F(CostRepoTest, ItemsSerialization_AllCategories) {
    auto est = makeRecord("All Categories");
    est.items.push_back(makeItem("Wood", dw::CostCategory::Material, 1.0, 10.0));
    est.items.push_back(makeItem("Cutting", dw::CostCategory::Labor, 2.0, 25.0));
    est.items.push_back(makeItem("Saw Blade", dw::CostCategory::Tool, 1.0, 15.0));
    est.items.push_back(makeItem("Shipping", dw::CostCategory::Other, 1.0, 8.0));
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->items.size(), 4u);

    EXPECT_EQ(found->items[0].name, "Wood");
    EXPECT_EQ(found->items[0].category, dw::CostCategory::Material);
    EXPECT_DOUBLE_EQ(found->items[0].quantity, 1.0);
    EXPECT_DOUBLE_EQ(found->items[0].rate, 10.0);
    EXPECT_DOUBLE_EQ(found->items[0].total, 10.0);

    EXPECT_EQ(found->items[1].name, "Cutting");
    EXPECT_EQ(found->items[1].category, dw::CostCategory::Labor);
    EXPECT_DOUBLE_EQ(found->items[1].quantity, 2.0);
    EXPECT_DOUBLE_EQ(found->items[1].rate, 25.0);
    EXPECT_DOUBLE_EQ(found->items[1].total, 50.0);

    EXPECT_EQ(found->items[2].name, "Saw Blade");
    EXPECT_EQ(found->items[2].category, dw::CostCategory::Tool);
    EXPECT_DOUBLE_EQ(found->items[2].quantity, 1.0);
    EXPECT_DOUBLE_EQ(found->items[2].rate, 15.0);
    EXPECT_DOUBLE_EQ(found->items[2].total, 15.0);

    EXPECT_EQ(found->items[3].name, "Shipping");
    EXPECT_EQ(found->items[3].category, dw::CostCategory::Other);
    EXPECT_DOUBLE_EQ(found->items[3].quantity, 1.0);
    EXPECT_DOUBLE_EQ(found->items[3].rate, 8.0);
    EXPECT_DOUBLE_EQ(found->items[3].total, 8.0);
}

TEST_F(CostRepoTest, ItemsSerialization_PreservesNotes) {
    auto est = makeRecord("Notes Test");
    dw::CostItem item;
    item.name = "Special Part";
    item.category = dw::CostCategory::Material;
    item.quantity = 1.0;
    item.rate = 99.99;
    item.total = 99.99;
    item.notes = "Custom order from supplier";
    est.items.push_back(item);

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->items.size(), 1u);
    EXPECT_EQ(found->items[0].notes, "Custom order from supplier");
}

TEST_F(CostRepoTest, ItemsSerialization_ManyItems) {
    auto est = makeRecord("Many Items");
    for (int i = 0; i < 10; ++i) {
        est.items.push_back(
            makeItem("Item " + std::to_string(i), dw::CostCategory::Material, 1.0, 10.0 + i));
    }
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->items.size(), 10u);

    for (int i = 0; i < 10; ++i) {
        const auto& item = found->items[static_cast<size_t>(i)];
        EXPECT_EQ(item.name, "Item " + std::to_string(i));
        EXPECT_DOUBLE_EQ(item.rate, 10.0 + i);
    }
}

// --- Timestamps ---

TEST_F(CostRepoTest, Insert_SetsTimestamps) {
    auto id = m_repo->insert(makeRecord("Timestamp Test"));
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->createdAt.empty());
    EXPECT_FALSE(found->modifiedAt.empty());
}

// --- Round-trip integrity ---

TEST_F(CostRepoTest, RoundTrip_FullEstimate) {
    dw::i64 pid = createProject("Round Trip Project");
    auto est = makeRecord("Full Round Trip", pid);
    est.items.push_back(makeItem("Lumber", dw::CostCategory::Material, 5.0, 12.0));
    est.items.push_back(makeItem("Nails", dw::CostCategory::Material, 100.0, 0.05));
    est.items.push_back(makeItem("Workshop Time", dw::CostCategory::Labor, 3.0, 35.0));
    est.items.push_back(makeItem("Router Bit", dw::CostCategory::Tool, 1.0, 22.0));
    est.items.push_back(makeItem("Delivery", dw::CostCategory::Other, 1.0, 15.0));
    est.taxRate = 7.5;
    est.discountRate = 2.0;
    est.notes = "Complete woodworking project estimate";
    est.recalculate();

    auto id = m_repo->insert(est);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());

    EXPECT_EQ(found->name, "Full Round Trip");
    EXPECT_EQ(found->projectId, pid);
    EXPECT_EQ(found->notes, "Complete woodworking project estimate");
    ASSERT_EQ(found->items.size(), 5u);

    // Verify calculated totals
    // subtotal = 60 + 5 + 105 + 22 + 15 = 207
    EXPECT_DOUBLE_EQ(found->subtotal, 207.0);
    EXPECT_DOUBLE_EQ(found->taxRate, 7.5);
    EXPECT_NEAR(found->taxAmount, 207.0 * 0.075, 1e-9);
    EXPECT_DOUBLE_EQ(found->discountRate, 2.0);
    EXPECT_NEAR(found->discountAmount, 207.0 * 0.02, 1e-9);
    EXPECT_NEAR(found->total, 207.0 + 207.0 * 0.075 - 207.0 * 0.02, 1e-9);
}
