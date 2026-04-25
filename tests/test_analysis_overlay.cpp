// Digital Workshop - Analysis Overlay Tests

#include <gtest/gtest.h>

#include "core/carve/analysis_overlay.h"

#include <vector>

namespace {

using dw::f32;

// Helper: build a simple flat heightmap
dw::carve::Heightmap buildFlat(f32 size, f32 z, f32 res) {
    const int gridN = static_cast<int>(size / res) + 1;
    std::vector<dw::Vertex> verts;
    std::vector<dw::u32> indices;

    for (int r = 0; r < gridN; ++r) {
        for (int c = 0; c < gridN; ++c) {
            const f32 x = static_cast<f32>(c) * res;
            const f32 y = static_cast<f32>(r) * res;
            verts.push_back(dw::Vertex({x, y, z}));
        }
    }
    for (int r = 0; r < gridN - 1; ++r) {
        for (int c = 0; c < gridN - 1; ++c) {
            const auto i = static_cast<dw::u32>(r * gridN + c);
            indices.push_back(i);
            indices.push_back(i + 1);
            indices.push_back(i + static_cast<dw::u32>(gridN));
            indices.push_back(i + 1);
            indices.push_back(i + static_cast<dw::u32>(gridN) + 1);
            indices.push_back(i + static_cast<dw::u32>(gridN));
        }
    }

    dw::carve::Heightmap hm;
    dw::carve::HeightmapConfig cfg;
    cfg.resolutionMm = res;
    hm.build(verts, indices,
             dw::Vec3(0.0f), dw::Vec3(size, size, z), cfg);
    return hm;
}

dw::carve::Heightmap buildYGradient(f32 size, f32 res) {
    std::vector<dw::Vertex> verts;
    verts.push_back(dw::Vertex({0.0f, 0.0f, 0.0f}));
    verts.push_back(dw::Vertex({size, 0.0f, 0.0f}));
    verts.push_back(dw::Vertex({size, size, size}));
    verts.push_back(dw::Vertex({0.0f, size, size}));

    const std::vector<dw::u32> indices = {0, 1, 2, 0, 2, 3};

    dw::carve::Heightmap hm;
    dw::carve::HeightmapConfig cfg;
    cfg.resolutionMm = res;
    hm.build(verts, indices,
             dw::Vec3(0.0f),
             dw::Vec3(size, size, size),
             cfg);
    return hm;
}

dw::u8 redAt(const std::vector<dw::u8>& pixels, int width, int col, int row)
{
    const auto index = static_cast<size_t>((row * width + col) * 4);
    return pixels[index];
}

} // namespace

TEST(AnalysisOverlay, EmptyHeightmap) {
    dw::carve::Heightmap hm;
    dw::carve::IslandResult islands;
    dw::carve::CurvatureResult curvature;

    auto pixels = dw::carve::generateAnalysisOverlay(hm, islands, curvature, 64, 64);
    EXPECT_TRUE(pixels.empty());
}

TEST(AnalysisOverlay, CorrectDimensions) {
    auto hm = buildFlat(10.0f, 5.0f, 1.0f);
    dw::carve::IslandResult islands;
    dw::carve::CurvatureResult curvature;

    const int w = 32;
    const int h = 32;
    auto pixels = dw::carve::generateAnalysisOverlay(hm, islands, curvature, w, h);
    EXPECT_EQ(pixels.size(), static_cast<size_t>(w * h * 4));
}

TEST(AnalysisOverlay, TopDisplayRowUsesMaxY) {
    auto hm = buildYGradient(10.0f, 1.0f);
    dw::carve::IslandResult islands;
    dw::carve::CurvatureResult curvature;

    auto pixels = dw::carve::generateAnalysisOverlay(
        hm, islands, curvature, hm.cols(), hm.rows());

    ASSERT_FALSE(pixels.empty());
    EXPECT_GT(redAt(pixels, hm.cols(), 0, 0),
              redAt(pixels, hm.cols(), 0, hm.rows() - 1));
}
