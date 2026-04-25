#include <gtest/gtest.h>

#include "core/carve/heightmap.h"
#include "core/carve/heightmap_preview.h"

#include <vector>

namespace {

dw::carve::Heightmap makeYGradientHeightmap()
{
    std::vector<dw::Vertex> verts;
    verts.push_back(dw::Vertex({0.0f, 0.0f, 0.0f}));
    verts.push_back(dw::Vertex({10.0f, 0.0f, 0.0f}));
    verts.push_back(dw::Vertex({10.0f, 10.0f, 10.0f}));
    verts.push_back(dw::Vertex({0.0f, 10.0f, 10.0f}));

    const std::vector<dw::u32> indices = {0, 1, 2, 0, 2, 3};

    dw::carve::Heightmap hm;
    dw::carve::HeightmapConfig cfg;
    cfg.resolutionMm = 1.0f;
    hm.build(verts, indices,
             dw::Vec3(0.0f, 0.0f, 0.0f),
             dw::Vec3(10.0f, 10.0f, 10.0f),
             cfg);
    return hm;
}

dw::u8 redAt(const std::vector<dw::u8>& pixels, int width, int col, int row)
{
    const auto index = static_cast<size_t>((row * width + col) * 4);
    return pixels[index];
}

} // namespace

TEST(HeightmapPreview, TopDisplayRowUsesMaxY)
{
    const auto hm = makeYGradientHeightmap();

    const auto pixels = dw::carve::generateHeightmapPreviewPixels(hm);

    ASSERT_FALSE(pixels.empty());
    ASSERT_EQ(pixels.size(),
              static_cast<size_t>(hm.cols() * hm.rows() * 4));
    EXPECT_GT(redAt(pixels, hm.cols(), 0, 0),
              redAt(pixels, hm.cols(), 0, hm.rows() - 1));
}
