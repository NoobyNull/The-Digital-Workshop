// Digital Workshop - Background Tagger Tests
// Tests tag_status transitions in the database without requiring Gemini API.

#include <gtest/gtest.h>

#include "core/database/database.h"
#include "core/database/model_repository.h"
#include "core/database/schema.h"

namespace {

class TagStatusTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_db.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_db));
    }

    dw::i64 insertTestModel(const std::string& name, const std::string& hash,
                            bool withThumbnail = true) {
        dw::ModelRecord record;
        record.hash = hash;
        record.name = name;
        record.filePath = "/tmp/" + name + ".stl";
        record.fileFormat = "stl";
        if (withThumbnail)
            record.thumbnailPath = "/tmp/" + name + ".tga";
        auto id = m_repo.insert(record);
        EXPECT_TRUE(id.has_value());
        return id.value_or(0);
    }

    dw::Database m_db;
    dw::ModelRepository m_repo{m_db};
};

} // namespace

TEST_F(TagStatusTest, DefaultIsUntagged) {
    auto id = insertTestModel("test", "hash1");
    auto model = m_repo.findById(id);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 0);
}

TEST_F(TagStatusTest, UpdateTagStatus) {
    auto id = insertTestModel("test", "hash1");

    EXPECT_TRUE(m_repo.updateTagStatus(id, 1)); // queued
    auto model = m_repo.findById(id);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 1);

    EXPECT_TRUE(m_repo.updateTagStatus(id, 2)); // tagged
    model = m_repo.findById(id);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 2);

    EXPECT_TRUE(m_repo.updateTagStatus(id, 3)); // failed
    model = m_repo.findById(id);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 3);

    EXPECT_TRUE(m_repo.updateTagStatus(id, 4)); // unclassifiable/manual review
    model = m_repo.findById(id);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->tagStatus, 4);
}

TEST_F(TagStatusTest, FindNextUntagged_ReturnsUntaggedWithThumbnail) {
    auto id1 = insertTestModel("tagged", "hash1");
    m_repo.updateTagStatus(id1, 2);

    auto id2 = insertTestModel("no_thumb", "hash2", false);
    (void)id2;

    auto id3 = insertTestModel("untagged", "hash3");
    (void)id3;

    auto next = m_repo.findNextUntagged();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->name, "untagged");
}

TEST_F(TagStatusTest, FindNextUntagged_ReturnsNulloptWhenAllTagged) {
    auto id1 = insertTestModel("a", "hash1");
    auto id2 = insertTestModel("b", "hash2");
    m_repo.updateTagStatus(id1, 2);
    m_repo.updateTagStatus(id2, 2);

    auto next = m_repo.findNextUntagged();
    EXPECT_FALSE(next.has_value());
}

TEST_F(TagStatusTest, CountByTagStatus) {
    auto id1 = insertTestModel("a", "hash1");
    auto id2 = insertTestModel("b", "hash2");
    auto id3 = insertTestModel("c", "hash3");

    EXPECT_EQ(m_repo.countByTagStatus(0), 3);
    EXPECT_EQ(m_repo.countByTagStatus(2), 0);

    m_repo.updateTagStatus(id1, 2);
    m_repo.updateTagStatus(id2, 3);
    (void)id3;

    EXPECT_EQ(m_repo.countByTagStatus(0), 1);
    EXPECT_EQ(m_repo.countByTagStatus(2), 1);
    EXPECT_EQ(m_repo.countByTagStatus(3), 1);
}

TEST_F(TagStatusTest, ResetTagStatus_RecoversQueuedRows) {
    auto queued = insertTestModel("queued", "hash1");
    auto tagged = insertTestModel("tagged", "hash2");
    auto failed = insertTestModel("failed", "hash3");
    ASSERT_TRUE(m_repo.updateTagStatus(queued, 1));
    ASSERT_TRUE(m_repo.updateTagStatus(tagged, 2));
    ASSERT_TRUE(m_repo.updateTagStatus(failed, 3));

    EXPECT_EQ(m_repo.resetTagStatus(1, 0), 1);

    auto recovered = m_repo.findById(queued);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered->tagStatus, 0);

    auto stillTagged = m_repo.findById(tagged);
    ASSERT_TRUE(stillTagged.has_value());
    EXPECT_EQ(stillTagged->tagStatus, 2);

    auto stillFailed = m_repo.findById(failed);
    ASSERT_TRUE(stillFailed.has_value());
    EXPECT_EQ(stillFailed->tagStatus, 3);
}

TEST_F(TagStatusTest, RecoverInterruptedTagStatuses_SplitsBlankAndDescribedRows) {
    auto blankQueued = insertTestModel("blank", "hash1");
    auto describedQueued = insertTestModel("described", "hash2");
    auto tagsOnlyQueued = insertTestModel("tags_only", "hash3");
    auto incompleteTagged = insertTestModel("incomplete_tagged", "hash4");
    auto tagged = insertTestModel("tagged", "hash5");

    ASSERT_TRUE(m_repo.updateTagStatus(blankQueued, 1));
    ASSERT_TRUE(m_repo.updateTagStatus(describedQueued, 1));
    ASSERT_TRUE(m_repo.updateDescriptor(describedQueued, "Carved Panel", "A relief panel", ""));
    ASSERT_TRUE(m_repo.updateTagStatus(tagsOnlyQueued, 1));
    ASSERT_TRUE(m_repo.updateTags(tagsOnlyQueued, {"relief", "panel"}));
    ASSERT_TRUE(m_repo.updateTagStatus(incompleteTagged, 2));
    ASSERT_TRUE(m_repo.updateTags(incompleteTagged, {"mythology", "bas-relief"}));
    ASSERT_TRUE(m_repo.updateTagStatus(tagged, 2));
    ASSERT_TRUE(m_repo.updateDescriptor(tagged, "Finished Tag", "A complete descriptor", ""));

    EXPECT_EQ(m_repo.recoverInterruptedTagStatuses(), 4);

    auto blank = m_repo.findById(blankQueued);
    ASSERT_TRUE(blank.has_value());
    EXPECT_EQ(blank->tagStatus, 0);

    auto described = m_repo.findById(describedQueued);
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->tagStatus, 2);

    auto tagsOnly = m_repo.findById(tagsOnlyQueued);
    ASSERT_TRUE(tagsOnly.has_value());
    EXPECT_EQ(tagsOnly->tagStatus, 0);

    auto incomplete = m_repo.findById(incompleteTagged);
    ASSERT_TRUE(incomplete.has_value());
    EXPECT_EQ(incomplete->tagStatus, 0);

    auto alreadyTagged = m_repo.findById(tagged);
    ASSERT_TRUE(alreadyTagged.has_value());
    EXPECT_EQ(alreadyTagged->tagStatus, 2);
}
