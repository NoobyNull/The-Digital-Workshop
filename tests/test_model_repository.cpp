// Digital Workshop - Model Repository Tests

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/model_repository.h"
#include "core/database/schema.h"

namespace {

// Fixture: in-memory DB with schema initialized
class ModelRepoTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
        m_repo = std::make_unique<dw::ModelRepository>(m_db);
    }

    dw::ModelRecord makeModel(const std::string& hash, const std::string& name) {
        dw::ModelRecord rec;
        rec.hash = hash;
        rec.name = name;
        rec.filePath = "/models/" + name + ".stl";
        rec.fileFormat = "stl";
        rec.fileSize = 1024;
        rec.vertexCount = 100;
        rec.triangleCount = 50;
        rec.boundsMin = dw::Vec3(0, 0, 0);
        rec.boundsMax = dw::Vec3(1, 1, 1);
        return rec;
    }

    dw::Database m_db;
    std::unique_ptr<dw::ModelRepository> m_repo;
};

} // namespace

// --- Insert ---

TEST_F(ModelRepoTest, Insert_ReturnsId) {
    auto rec = makeModel("abc123", "cube");
    auto id = m_repo->insert(rec);
    ASSERT_TRUE(id.has_value());
    EXPECT_GT(id.value(), 0);
}

TEST_F(ModelRepoTest, Insert_DuplicateHashFails) {
    auto rec1 = makeModel("same_hash", "model_a");
    auto rec2 = makeModel("same_hash", "model_b");

    EXPECT_TRUE(m_repo->insert(rec1).has_value());
    EXPECT_FALSE(m_repo->insert(rec2).has_value());
}

// --- FindById ---

TEST_F(ModelRepoTest, FindById_Found) {
    auto rec = makeModel("hash1", "widget");
    auto id = m_repo->insert(rec);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "widget");
    EXPECT_EQ(found->hash, "hash1");
    EXPECT_EQ(found->fileFormat, "stl");
    EXPECT_EQ(found->fileSize, 1024u);
    EXPECT_EQ(found->vertexCount, 100u);
    EXPECT_EQ(found->triangleCount, 50u);
}

TEST_F(ModelRepoTest, FindById_NotFound) {
    auto found = m_repo->findById(999);
    EXPECT_FALSE(found.has_value());
}

// --- FindByHash ---

TEST_F(ModelRepoTest, FindByHash_Found) {
    auto rec = makeModel("unique_hash", "test_model");
    m_repo->insert(rec);

    auto found = m_repo->findByHash("unique_hash");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "test_model");
}

TEST_F(ModelRepoTest, FindByHash_NotFound) {
    auto found = m_repo->findByHash("nonexistent");
    EXPECT_FALSE(found.has_value());
}

// --- FindAll ---

TEST_F(ModelRepoTest, FindAll_Empty) {
    auto all = m_repo->findAll();
    EXPECT_TRUE(all.empty());
}

TEST_F(ModelRepoTest, FindAll_Multiple) {
    m_repo->insert(makeModel("h1", "model_a"));
    m_repo->insert(makeModel("h2", "model_b"));
    m_repo->insert(makeModel("h3", "model_c"));

    auto all = m_repo->findAll();
    EXPECT_EQ(all.size(), 3u);
}

// --- FindByName ---

TEST_F(ModelRepoTest, FindByName_Matches) {
    m_repo->insert(makeModel("h1", "widget_small"));
    m_repo->insert(makeModel("h2", "widget_large"));
    m_repo->insert(makeModel("h3", "gadget"));

    auto results = m_repo->findByName("widget");
    EXPECT_EQ(results.size(), 2u);
}

TEST_F(ModelRepoTest, FindByName_NoMatch) {
    m_repo->insert(makeModel("h1", "widget"));
    auto results = m_repo->findByName("nonexistent");
    EXPECT_TRUE(results.empty());
}

// --- FindByFormat ---

TEST_F(ModelRepoTest, FindByFormat) {
    auto stl = makeModel("h1", "stl_model");
    stl.fileFormat = "stl";
    m_repo->insert(stl);

    auto obj = makeModel("h2", "obj_model");
    obj.fileFormat = "obj";
    m_repo->insert(obj);

    auto stls = m_repo->findByFormat("stl");
    EXPECT_EQ(stls.size(), 1u);
    EXPECT_EQ(stls[0].name, "stl_model");
}

// --- Update ---

TEST_F(ModelRepoTest, Update_ChangesName) {
    auto rec = makeModel("h1", "old_name");
    auto id = m_repo->insert(rec);
    ASSERT_TRUE(id.has_value());

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    found->name = "new_name";
    EXPECT_TRUE(m_repo->update(*found));

    auto updated = m_repo->findById(id.value());
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->name, "new_name");
}

// --- UpdateThumbnail ---

TEST_F(ModelRepoTest, UpdateThumbnail) {
    auto id = m_repo->insert(makeModel("h1", "model"));
    ASSERT_TRUE(id.has_value());

    EXPECT_TRUE(m_repo->updateThumbnail(id.value(), "/thumbs/model.tga"));

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->thumbnailPath, dw::Path("/thumbs/model.tga"));
}

// --- UpdateTags ---

TEST_F(ModelRepoTest, UpdateTags) {
    auto id = m_repo->insert(makeModel("h1", "model"));
    ASSERT_TRUE(id.has_value());

    std::vector<std::string> tags = {"furniture", "wood", "table"};
    EXPECT_TRUE(m_repo->updateTags(id.value(), tags));

    auto found = m_repo->findById(id.value());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->tags.size(), 3u);
}

TEST_F(ModelRepoTest, FindByTag) {
    auto id = m_repo->insert(makeModel("h1", "chair"));
    ASSERT_TRUE(id.has_value());
    m_repo->updateTags(id.value(), {"furniture", "wood"});

    auto id2 = m_repo->insert(makeModel("h2", "bolt"));
    ASSERT_TRUE(id2.has_value());
    m_repo->updateTags(id2.value(), {"hardware", "metal"});

    auto results = m_repo->findByTag("furniture");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "chair");
}

TEST_F(ModelRepoTest, FindByTagStatus) {
    auto id1 = m_repo->insert(makeModel("hash-review-a", "review_a"));
    auto id2 = m_repo->insert(makeModel("hash-tagged-b", "tagged_b"));
    auto id3 = m_repo->insert(makeModel("hash-review-c", "review_c"));
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    ASSERT_TRUE(id3.has_value());

    EXPECT_TRUE(m_repo->updateTagStatus(*id1, 4));
    EXPECT_TRUE(m_repo->updateTagStatus(*id2, 2));
    EXPECT_TRUE(m_repo->updateTagStatus(*id3, 4));

    auto results = m_repo->findByTagStatus(4);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].tagStatus, 4);
    EXPECT_EQ(results[1].tagStatus, 4);
}

// --- Remove ---

TEST_F(ModelRepoTest, Remove_ById) {
    auto id = m_repo->insert(makeModel("h1", "model"));
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(m_repo->count(), 1);

    EXPECT_TRUE(m_repo->remove(id.value()));
    EXPECT_EQ(m_repo->count(), 0);
}

TEST_F(ModelRepoTest, RemoveByHash) {
    m_repo->insert(makeModel("target_hash", "model"));
    EXPECT_EQ(m_repo->count(), 1);

    EXPECT_TRUE(m_repo->removeByHash("target_hash"));
    EXPECT_EQ(m_repo->count(), 0);
}

// --- Exists / Count ---

TEST_F(ModelRepoTest, Exists_True) {
    m_repo->insert(makeModel("test_hash", "model"));
    EXPECT_TRUE(m_repo->exists("test_hash"));
}

TEST_F(ModelRepoTest, Exists_False) {
    EXPECT_FALSE(m_repo->exists("nonexistent"));
}

TEST_F(ModelRepoTest, Count) {
    EXPECT_EQ(m_repo->count(), 0);
    m_repo->insert(makeModel("h1", "a"));
    EXPECT_EQ(m_repo->count(), 1);
    m_repo->insert(makeModel("h2", "b"));
    EXPECT_EQ(m_repo->count(), 2);
}
