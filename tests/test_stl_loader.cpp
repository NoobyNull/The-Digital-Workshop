// Digital Workshop - STL Loader Tests

#include <gtest/gtest.h>

#include "core/loaders/stl_loader.h"

#include <array>
#include <cstring>

namespace {

// Build a minimal valid binary STL with the given number of triangles.
// Binary STL format: 80-byte header + 4-byte triangle count + N * 50 bytes
dw::ByteBuffer makeBinarySTL(dw::u32 triangleCount,
                             const std::vector<std::array<float, 12>>& triangles = {}) {
    dw::usize size = 80 + 4 + static_cast<dw::usize>(triangleCount) * 50;
    dw::ByteBuffer buf(size, 0);

    // Write triangle count at offset 80
    std::memcpy(buf.data() + 80, &triangleCount, sizeof(triangleCount));

    // Write triangle data if provided
    dw::u8* ptr = buf.data() + 84;
    for (dw::u32 i = 0; i < triangleCount; ++i) {
        if (i < triangles.size()) {
            // Normal (3 floats) + 3 vertices (9 floats) = 12 floats = 48 bytes
            std::memcpy(ptr, triangles[i].data(), 12 * sizeof(float));
        }
        ptr += 50; // 48 bytes geometry + 2 bytes attribute
    }

    return buf;
}

} // namespace

TEST(STLLoader, LoadFromBuffer_SingleTriangle) {
    // One triangle: normal=(0,0,1), v0=(0,0,0), v1=(1,0,0), v2=(0,1,0)
    std::array<float, 12> tri = {
        0.0f,
        0.0f,
        1.0f, // normal
        0.0f,
        0.0f,
        0.0f, // vertex 0
        1.0f,
        0.0f,
        0.0f, // vertex 1
        0.0f,
        1.0f,
        0.0f // vertex 2
    };

    auto data = makeBinarySTL(1, {tri});
    ASSERT_EQ(data.size(), 80u + 4u + 50u);

    dw::STLLoader loader;
    auto result = loader.loadFromBuffer(data);

    ASSERT_TRUE(result.success()) << "Error: " << result.error;
    ASSERT_NE(result.mesh, nullptr);
    EXPECT_EQ(result.mesh->triangleCount(), 1u);
    EXPECT_EQ(result.mesh->vertexCount(), 3u);

    // Verify vertex positions
    const auto& verts = result.mesh->vertices();
    EXPECT_FLOAT_EQ(verts[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].position.y, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].position.z, 0.0f);

    EXPECT_FLOAT_EQ(verts[1].position.x, 1.0f);
    EXPECT_FLOAT_EQ(verts[1].position.y, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].position.z, 0.0f);

    EXPECT_FLOAT_EQ(verts[2].position.x, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].position.y, 1.0f);
    EXPECT_FLOAT_EQ(verts[2].position.z, 0.0f);
}

TEST(STLLoader, LoadFromBuffer_RebuildsNormalsFromTriangleWinding) {
    // Stored normal deliberately disagrees with winding. The loader should trust geometry,
    // because stale exported STL normals cause visibly dark islands under lighting.
    std::array<float, 12> tri = {
        0.0f,
        0.0f,
        -1.0f, // incorrect stored normal
        0.0f,
        0.0f,
        0.0f, // vertex 0
        1.0f,
        0.0f,
        0.0f, // vertex 1
        0.0f,
        1.0f,
        0.0f // vertex 2; winding normal is +Z
    };

    auto data = makeBinarySTL(1, {tri});

    dw::STLLoader loader;
    auto result = loader.loadFromBuffer(data);

    ASSERT_TRUE(result.success()) << "Error: " << result.error;
    ASSERT_NE(result.mesh, nullptr);

    for (const auto& vertex : result.mesh->vertices()) {
        EXPECT_NEAR(vertex.normal.x, 0.0f, 1e-5f);
        EXPECT_NEAR(vertex.normal.y, 0.0f, 1e-5f);
        EXPECT_NEAR(vertex.normal.z, 1.0f, 1e-5f);
    }
}

TEST(STLLoader, LoadFromBuffer_EmptyBuffer) {
    dw::ByteBuffer empty;
    dw::STLLoader loader;
    auto result = loader.loadFromBuffer(empty);

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.mesh, nullptr);
    EXPECT_FALSE(result.error.empty());
}

TEST(STLLoader, LoadFromBuffer_TooSmallBuffer) {
    // A buffer that is smaller than the 84-byte binary STL minimum
    dw::ByteBuffer small(40, 0);
    dw::STLLoader loader;
    auto result = loader.loadFromBuffer(small);

    // Too small for binary; will try ASCII parse and fail because no triangles found
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.mesh, nullptr);
}

TEST(STLLoader, LoadFromBuffer_TwoTrianglesWithSharedVertices) {
    // Two triangles sharing an edge: forms a quad
    // Triangle 1: (0,0,0), (1,0,0), (0,1,0)
    // Triangle 2: (1,0,0), (1,1,0), (0,1,0)  -- shares v1=(1,0,0) and v2=(0,1,0)
    std::array<float, 12> tri1 = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 12> tri2 = {
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};

    auto data = makeBinarySTL(2, {tri1, tri2});
    dw::STLLoader loader;
    auto result = loader.loadFromBuffer(data);

    ASSERT_TRUE(result.success()) << "Error: " << result.error;
    EXPECT_EQ(result.mesh->triangleCount(), 2u);
    // Binary STL uses flat arrays (no dedup) for performance: 2 triangles * 3 = 6 verts
    EXPECT_EQ(result.mesh->vertexCount(), 6u);
}

TEST(STLLoader, SupportsExtension) {
    dw::STLLoader loader;
    EXPECT_TRUE(loader.supports("stl"));
    EXPECT_TRUE(loader.supports("STL"));
    EXPECT_FALSE(loader.supports("obj"));
    EXPECT_FALSE(loader.supports(""));
}
