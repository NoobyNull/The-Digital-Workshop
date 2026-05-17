#include <gtest/gtest.h>

#include "core/mesh/mesh_preview.h"

namespace {

dw::Mesh makeTriangleStripLikeMesh(int triangleCount) {
    std::vector<dw::Vertex> vertices;
    std::vector<dw::u32> indices;
    vertices.reserve(static_cast<size_t>(triangleCount) * 3);
    indices.reserve(static_cast<size_t>(triangleCount) * 3);

    for (int i = 0; i < triangleCount; ++i) {
        auto base = static_cast<dw::u32>(vertices.size());
        float x = static_cast<float>(i);
        vertices.emplace_back(dw::Vec3{x, 0.0f, 0.0f});
        vertices.emplace_back(dw::Vec3{x, 1.0f, 0.0f});
        vertices.emplace_back(dw::Vec3{x, 0.0f, 1.0f});
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    return dw::Mesh(std::move(vertices), std::move(indices));
}

} // namespace

TEST(MeshPreview, LeavesSmallMeshesIntact) {
    auto mesh = makeTriangleStripLikeMesh(3);

    auto preview = dw::mesh_preview::sampleTrianglesForPreview(mesh, 10);

    EXPECT_EQ(preview.triangleCount(), mesh.triangleCount());
    EXPECT_EQ(preview.vertexCount(), mesh.vertexCount());
}

TEST(MeshPreview, CapsLargeMeshesToRequestedTriangleCount) {
    auto mesh = makeTriangleStripLikeMesh(100);

    auto preview = dw::mesh_preview::sampleTrianglesForPreview(mesh, 10);

    EXPECT_EQ(preview.triangleCount(), 10u);
    EXPECT_EQ(preview.indexCount(), 30u);
    EXPECT_EQ(preview.vertexCount(), 30u);
    EXPECT_TRUE(preview.isValid());
}

TEST(MeshPreview, SamplesAcrossFullTriangleRange) {
    auto mesh = makeTriangleStripLikeMesh(100);

    auto preview = dw::mesh_preview::sampleTrianglesForPreview(mesh, 10);

    EXPECT_FLOAT_EQ(preview.bounds().min.x, 0.0f);
    EXPECT_FLOAT_EQ(preview.bounds().max.x, 99.0f);
}
