#include <gtest/gtest.h>

#include "core/mesh/mesh.h"
#include "core/mesh/mesh_repair.h"

namespace {

dw::Mesh makeTriangleWithNormals(const dw::Vec3& normal) {
    std::vector<dw::Vertex> vertices = {
        dw::Vertex({0.0f, 0.0f, 0.0f}),
        dw::Vertex({1.0f, 0.0f, 0.0f}),
        dw::Vertex({0.0f, 1.0f, 0.0f}),
    };
    for (auto& vertex : vertices) {
        vertex.normal = normal;
    }
    return dw::Mesh(std::move(vertices), {0, 1, 2});
}

} // namespace

TEST(MeshRepair, RecalculateNormalsUpdatesValidMesh) {
    auto mesh = makeTriangleWithNormals(dw::Vec3{0.0f, 0.0f, -1.0f});
    auto hashBefore = mesh.geometryHash();

    auto result = dw::mesh_repair::recalculateNormals(mesh);

    EXPECT_TRUE(result.repaired);
    EXPECT_EQ(result.previousHash, hashBefore);
    EXPECT_EQ(result.newHash, mesh.geometryHash());
    EXPECT_NE(result.previousHash, result.newHash);
    for (const auto& vertex : mesh.vertices()) {
        EXPECT_NEAR(vertex.normal.x, 0.0f, 1e-5f);
        EXPECT_NEAR(vertex.normal.y, 0.0f, 1e-5f);
        EXPECT_NEAR(vertex.normal.z, 1.0f, 1e-5f);
    }
}

TEST(MeshRepair, RecalculateNormalsSkipsInvalidMesh) {
    dw::Mesh mesh;

    auto result = dw::mesh_repair::recalculateNormals(mesh);

    EXPECT_FALSE(result.repaired);
    EXPECT_EQ(result.previousHash, 0u);
    EXPECT_EQ(result.newHash, 0u);
}
