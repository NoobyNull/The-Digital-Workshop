#include <gtest/gtest.h>

#include "../src/app/workspace.h"
#include "../src/core/mesh/mesh.h"
#include "../src/core/utils/thread_utils.h"

namespace dw {

class WorkspaceTest : public ::testing::Test {
  protected:
    Workspace workspace;

    void SetUp() override { threading::initMainThread(); }

    // Helper to create a test mesh
    std::shared_ptr<Mesh> createTestMesh(size_t vertexCount = 3) {
        auto mesh = std::make_shared<Mesh>();
        for (size_t i = 0; i < vertexCount; ++i) {
            mesh->addVertex(
                Vertex(Vec3(static_cast<float>(i), 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
        }
        if (vertexCount >= 3) {
            mesh->addTriangle(0, 1, 2);
        }
        return mesh;
    }
};

// Test focused mesh operations
TEST_F(WorkspaceTest, SetAndGetFocusedMesh) {
    EXPECT_FALSE(workspace.hasFocusedMesh());
    EXPECT_EQ(workspace.getFocusedMesh(), nullptr);

    auto mesh = createTestMesh();
    workspace.setFocusedMesh(mesh);

    EXPECT_TRUE(workspace.hasFocusedMesh());
    EXPECT_EQ(workspace.getFocusedMesh(), mesh);
}

TEST_F(WorkspaceTest, ClearFocusedMesh) {
    auto mesh = createTestMesh();
    workspace.setFocusedMesh(mesh);
    EXPECT_TRUE(workspace.hasFocusedMesh());

    workspace.clearFocusedMesh();
    EXPECT_FALSE(workspace.hasFocusedMesh());
    EXPECT_EQ(workspace.getFocusedMesh(), nullptr);
}

TEST_F(WorkspaceTest, ReplaceFocusedMesh) {
    auto mesh1 = createTestMesh(3);
    auto mesh2 = createTestMesh(6);

    workspace.setFocusedMesh(mesh1);
    EXPECT_EQ(workspace.getFocusedMesh(), mesh1);

    workspace.setFocusedMesh(mesh2);
    EXPECT_EQ(workspace.getFocusedMesh(), mesh2);
}

TEST_F(WorkspaceTest, SetNullFocusedMesh) {
    auto mesh = createTestMesh();
    workspace.setFocusedMesh(mesh);
    EXPECT_TRUE(workspace.hasFocusedMesh());

    workspace.setFocusedMesh(nullptr);
    EXPECT_FALSE(workspace.hasFocusedMesh());
}

// Test focused toolpath operations
TEST_F(WorkspaceTest, SetAndGetFocusedToolpath) {
    EXPECT_FALSE(workspace.hasFocusedToolpath());
    EXPECT_EQ(workspace.getFocusedToolpath(), nullptr);

    auto toolpath = createTestMesh();
    workspace.setFocusedToolpath(toolpath);

    EXPECT_TRUE(workspace.hasFocusedToolpath());
    EXPECT_EQ(workspace.getFocusedToolpath(), toolpath);
}

TEST_F(WorkspaceTest, ClearFocusedToolpath) {
    auto toolpath = createTestMesh();
    workspace.setFocusedToolpath(toolpath);
    EXPECT_TRUE(workspace.hasFocusedToolpath());

    workspace.clearFocusedToolpath();
    EXPECT_FALSE(workspace.hasFocusedToolpath());
    EXPECT_EQ(workspace.getFocusedToolpath(), nullptr);
}

// Test that mesh and toolpath are independent
TEST_F(WorkspaceTest, MeshAndToolpathAreIndependent) {
    auto mesh = createTestMesh(3);
    auto toolpath = createTestMesh(6);

    workspace.setFocusedMesh(mesh);
    workspace.setFocusedToolpath(toolpath);

    EXPECT_EQ(workspace.getFocusedMesh(), mesh);
    EXPECT_EQ(workspace.getFocusedToolpath(), toolpath);

    workspace.clearFocusedMesh();
    EXPECT_FALSE(workspace.hasFocusedMesh());
    EXPECT_TRUE(workspace.hasFocusedToolpath());
    EXPECT_EQ(workspace.getFocusedToolpath(), toolpath);
}

// Test clearAll functionality
TEST_F(WorkspaceTest, ClearAllRemovesAllFocusedObjects) {
    workspace.setFocusedMesh(createTestMesh(3));
    workspace.setFocusedToolpath(createTestMesh(6));

    EXPECT_TRUE(workspace.hasFocusedMesh());
    EXPECT_TRUE(workspace.hasFocusedToolpath());

    workspace.clearAll();

    EXPECT_FALSE(workspace.hasFocusedMesh());
    EXPECT_FALSE(workspace.hasFocusedToolpath());
    EXPECT_EQ(workspace.getFocusedMesh(), nullptr);
    EXPECT_EQ(workspace.getFocusedToolpath(), nullptr);
}

// Test shared pointer reference counting
TEST_F(WorkspaceTest, WorkspaceDoesNotHoldExtraneousReferences) {
    {
        auto mesh = createTestMesh();
        auto refCountBefore = mesh.use_count(); // mesh + this scope
        workspace.setFocusedMesh(mesh);
        auto refCountAfter = mesh.use_count(); // mesh + this scope + workspace
        EXPECT_EQ(refCountAfter, refCountBefore + 1);

        workspace.clearFocusedMesh();
        auto refCountAfterClear = mesh.use_count();
        EXPECT_EQ(refCountAfterClear, refCountBefore);
    }
}

// Test multiple sets and clears
TEST_F(WorkspaceTest, MultipleSetAndClearCycles) {
    auto mesh1 = createTestMesh(3);
    auto mesh2 = createTestMesh(6);

    // Cycle 1
    workspace.setFocusedMesh(mesh1);
    EXPECT_EQ(workspace.getFocusedMesh(), mesh1);
    workspace.clearFocusedMesh();
    EXPECT_FALSE(workspace.hasFocusedMesh());

    // Cycle 2
    workspace.setFocusedMesh(mesh2);
    EXPECT_EQ(workspace.getFocusedMesh(), mesh2);
    workspace.clearFocusedMesh();
    EXPECT_FALSE(workspace.hasFocusedMesh());

    // Cycle 3 - set both and clear
    workspace.setFocusedMesh(mesh1);
    workspace.setFocusedToolpath(mesh2);
    EXPECT_TRUE(workspace.hasFocusedMesh());
    EXPECT_TRUE(workspace.hasFocusedToolpath());

    workspace.clearAll();
    EXPECT_FALSE(workspace.hasFocusedMesh());
    EXPECT_FALSE(workspace.hasFocusedToolpath());
}

} // namespace dw
