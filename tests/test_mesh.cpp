// Digital Workshop - Mesh Tests

#include <gtest/gtest.h>

#include "core/mesh/bounds.h"
#include "core/mesh/mesh.h"

#include <cmath>

namespace {

// Helper: create a simple triangle mesh
dw::Mesh makeTriangle() {
    std::vector<dw::Vertex> verts = {
        dw::Vertex({0.0f, 0.0f, 0.0f}),
        dw::Vertex({1.0f, 0.0f, 0.0f}),
        dw::Vertex({0.0f, 1.0f, 0.0f}),
    };
    std::vector<dw::u32> indices = {0, 1, 2};
    return dw::Mesh(std::move(verts), std::move(indices));
}

// Helper: create a unit cube mesh (8 verts, 12 triangles)
dw::Mesh makeCube() {
    std::vector<dw::Vertex> verts;
    for (int z = 0; z <= 1; ++z)
        for (int y = 0; y <= 1; ++y)
            for (int x = 0; x <= 1; ++x)
                verts.emplace_back(
                    dw::Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
    // 6 faces, 2 triangles each
    std::vector<dw::u32> idx = {
        0, 1, 3, 0, 3, 2, // -Z
        4, 6, 7, 4, 7, 5, // +Z
        0, 4, 5, 0, 5, 1, // -Y
        2, 3, 7, 2, 7, 6, // +Y
        0, 2, 6, 0, 6, 4, // -X
        1, 5, 7, 1, 7, 3, // +X
    };
    return dw::Mesh(std::move(verts), std::move(idx));
}

} // namespace

// --- Construction ---

TEST(Mesh, DefaultConstruction_Empty) {
    dw::Mesh mesh;
    EXPECT_EQ(mesh.vertexCount(), 0u);
    EXPECT_EQ(mesh.triangleCount(), 0u);
    EXPECT_EQ(mesh.indexCount(), 0u);
    EXPECT_FALSE(mesh.isValid());
}

TEST(Mesh, ConstructFromData) {
    auto mesh = makeTriangle();
    EXPECT_EQ(mesh.vertexCount(), 3u);
    EXPECT_EQ(mesh.triangleCount(), 1u);
    EXPECT_EQ(mesh.indexCount(), 3u);
    EXPECT_TRUE(mesh.isValid());
}

// --- Bounds ---

TEST(Mesh, Bounds_Triangle) {
    auto mesh = makeTriangle();
    const auto& b = mesh.bounds();
    EXPECT_FLOAT_EQ(b.min.x, 0.0f);
    EXPECT_FLOAT_EQ(b.min.y, 0.0f);
    EXPECT_FLOAT_EQ(b.min.z, 0.0f);
    EXPECT_FLOAT_EQ(b.max.x, 1.0f);
    EXPECT_FLOAT_EQ(b.max.y, 1.0f);
    EXPECT_FLOAT_EQ(b.max.z, 0.0f);
}

TEST(Mesh, Bounds_Cube) {
    auto mesh = makeCube();
    const auto& b = mesh.bounds();
    EXPECT_FLOAT_EQ(b.min.x, 0.0f);
    EXPECT_FLOAT_EQ(b.min.y, 0.0f);
    EXPECT_FLOAT_EQ(b.min.z, 0.0f);
    EXPECT_FLOAT_EQ(b.max.x, 1.0f);
    EXPECT_FLOAT_EQ(b.max.y, 1.0f);
    EXPECT_FLOAT_EQ(b.max.z, 1.0f);
}

TEST(Mesh, RecalculateBounds) {
    auto mesh = makeTriangle();
    // Manually move a vertex and recalculate
    mesh.vertices()[1].position = dw::Vec3(5.0f, 0.0f, 0.0f);
    mesh.recalculateBounds();
    EXPECT_FLOAT_EQ(mesh.bounds().max.x, 5.0f);
}

// --- Name ---

TEST(Mesh, SetName) {
    dw::Mesh mesh;
    mesh.setName("test_model");
    EXPECT_EQ(mesh.name(), "test_model");
}

// --- Clear ---

TEST(Mesh, Clear) {
    auto mesh = makeTriangle();
    ASSERT_TRUE(mesh.isValid());
    mesh.clear();
    EXPECT_EQ(mesh.vertexCount(), 0u);
    EXPECT_EQ(mesh.indexCount(), 0u);
    EXPECT_FALSE(mesh.isValid());
}

// --- Add vertex / triangle ---

TEST(Mesh, AddVertexAndTriangle) {
    dw::Mesh mesh;
    mesh.addVertex(dw::Vertex({0.0f, 0.0f, 0.0f}));
    mesh.addVertex(dw::Vertex({1.0f, 0.0f, 0.0f}));
    mesh.addVertex(dw::Vertex({0.0f, 1.0f, 0.0f}));
    mesh.addTriangle(0, 1, 2);

    EXPECT_EQ(mesh.vertexCount(), 3u);
    EXPECT_EQ(mesh.triangleCount(), 1u);
}

// --- Reserve ---

TEST(Mesh, Reserve_DoesNotChangeSize) {
    dw::Mesh mesh;
    mesh.reserve(100, 300);
    EXPECT_EQ(mesh.vertexCount(), 0u);
    EXPECT_EQ(mesh.indexCount(), 0u);
}

// --- Clone ---

TEST(Mesh, Clone_IndependentCopy) {
    auto original = makeTriangle();
    original.setName("original");

    auto clone = original.clone();
    EXPECT_EQ(clone.vertexCount(), original.vertexCount());
    EXPECT_EQ(clone.triangleCount(), original.triangleCount());
    EXPECT_EQ(clone.name(), original.name());

    // Modify clone, original unchanged
    clone.vertices()[0].position.x = 99.0f;
    EXPECT_FLOAT_EQ(original.vertices()[0].position.x, 0.0f);
}

// --- Merge ---

TEST(Mesh, Merge_CombinesGeometry) {
    auto mesh1 = makeTriangle();
    auto mesh2 = makeTriangle();

    dw::u32 origVerts = mesh1.vertexCount();
    dw::u32 origTris = mesh1.triangleCount();

    mesh1.merge(mesh2);

    EXPECT_EQ(mesh1.vertexCount(), origVerts + mesh2.vertexCount());
    EXPECT_EQ(mesh1.triangleCount(), origTris + mesh2.triangleCount());
}

// --- CenterOnOrigin ---

TEST(Mesh, CenterOnOrigin) {
    auto mesh = makeCube();
    mesh.centerOnOrigin();
    mesh.recalculateBounds();

    auto center = mesh.bounds().center();
    EXPECT_NEAR(center.x, 0.0f, 1e-5f);
    EXPECT_NEAR(center.y, 0.0f, 1e-5f);
    EXPECT_NEAR(center.z, 0.0f, 1e-5f);
}

// --- NormalizeSize ---

TEST(Mesh, NormalizeSize) {
    auto mesh = makeCube();
    // Scale cube (0-1) up, then normalize to target size 2.0
    mesh.transform(glm::scale(dw::Mat4(1.0f), dw::Vec3(10.0f, 10.0f, 10.0f)));
    mesh.normalizeSize(2.0f);
    mesh.recalculateBounds();

    float maxExtent = mesh.bounds().maxExtent();
    EXPECT_NEAR(maxExtent, 2.0f, 1e-4f);
}

// --- Transform ---

TEST(Mesh, Transform_Scale) {
    auto mesh = makeTriangle();
    mesh.transform(glm::scale(dw::Mat4(1.0f), dw::Vec3(2.0f, 2.0f, 2.0f)));
    mesh.recalculateBounds();

    EXPECT_FLOAT_EQ(mesh.bounds().max.x, 2.0f);
    EXPECT_FLOAT_EQ(mesh.bounds().max.y, 2.0f);
}

TEST(Mesh, Transform_Translate) {
    auto mesh = makeTriangle();
    mesh.transform(glm::translate(dw::Mat4(1.0f), dw::Vec3(10.0f, 0.0f, 0.0f)));
    mesh.recalculateBounds();

    EXPECT_NEAR(mesh.bounds().min.x, 10.0f, 1e-5f);
    EXPECT_NEAR(mesh.bounds().max.x, 11.0f, 1e-5f);
}

// --- Normals ---

TEST(Mesh, RecalculateNormals) {
    auto mesh = makeTriangle();
    // Zero out normals
    for (auto& v : mesh.vertices()) {
        v.normal = dw::Vec3(0, 0, 0);
    }
    EXPECT_FALSE(mesh.hasNormals());

    mesh.recalculateNormals();
    EXPECT_TRUE(mesh.hasNormals());

    // Triangle in XY plane should have normals pointing in +Z or -Z
    for (const auto& v : mesh.vertices()) {
        float nz = std::abs(v.normal.z);
        EXPECT_NEAR(nz, 1.0f, 1e-5f);
    }
}

TEST(Mesh, GeometryHashChangesWhenNormalsChange) {
    auto mesh = makeTriangle();
    for (auto& v : mesh.vertices()) {
        v.normal = dw::Vec3(0.0f, 0.0f, -1.0f);
    }
    auto hashBefore = mesh.geometryHash();

    mesh.recalculateNormals();

    EXPECT_NE(hashBefore, mesh.geometryHash());
}

TEST(Mesh, GeometryHashChangesWhenTexCoordsChange) {
    auto mesh = makeTriangle();
    auto hashBefore = mesh.geometryHash();

    mesh.generatePlanarUVs();

    EXPECT_NE(hashBefore, mesh.geometryHash());
}

// --- AABB tests (header-only) ---

TEST(AABB, DefaultInvalid) {
    dw::AABB box;
    EXPECT_FALSE(box.isValid());
}

TEST(AABB, ExpandPoints) {
    dw::AABB box;
    box.expand(dw::Vec3(0, 0, 0));
    box.expand(dw::Vec3(1, 2, 3));

    EXPECT_TRUE(box.isValid());
    EXPECT_FLOAT_EQ(box.width(), 1.0f);
    EXPECT_FLOAT_EQ(box.height(), 2.0f);
    EXPECT_FLOAT_EQ(box.depth(), 3.0f);
}

TEST(AABB, Center) {
    dw::AABB box(dw::Vec3(0, 0, 0), dw::Vec3(2, 4, 6));
    auto c = box.center();
    EXPECT_FLOAT_EQ(c.x, 1.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, 3.0f);
}

TEST(AABB, Contains) {
    dw::AABB box(dw::Vec3(0, 0, 0), dw::Vec3(10, 10, 10));
    EXPECT_TRUE(box.contains(dw::Vec3(5, 5, 5)));
    EXPECT_FALSE(box.contains(dw::Vec3(11, 5, 5)));
}

TEST(AABB, Intersects) {
    dw::AABB a(dw::Vec3(0, 0, 0), dw::Vec3(2, 2, 2));
    dw::AABB b(dw::Vec3(1, 1, 1), dw::Vec3(3, 3, 3));
    dw::AABB c(dw::Vec3(5, 5, 5), dw::Vec3(6, 6, 6));

    EXPECT_TRUE(a.intersects(b));
    EXPECT_FALSE(a.intersects(c));
}

TEST(AABB, MaxExtent) {
    dw::AABB box(dw::Vec3(0, 0, 0), dw::Vec3(1, 5, 3));
    EXPECT_FLOAT_EQ(box.maxExtent(), 5.0f);
}

TEST(AABB, Reset) {
    dw::AABB box(dw::Vec3(0, 0, 0), dw::Vec3(1, 1, 1));
    EXPECT_TRUE(box.isValid());
    box.reset();
    EXPECT_FALSE(box.isValid());
}
