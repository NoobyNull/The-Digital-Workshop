#include "heightmap_preview.h"

#include <algorithm>

namespace dw {
namespace carve {

std::vector<u8> generateHeightmapPreviewPixels(const Heightmap& heightmap)
{
    if (heightmap.empty()) return {};

    const int width = heightmap.cols();
    const int height = heightmap.rows();
    f32 range = heightmap.maxZ() - heightmap.minZ();
    if (range < 1e-6f) range = 1.0f;

    std::vector<u8> pixels(static_cast<size_t>(width * height * 4));
    for (int displayRow = 0; displayRow < height; ++displayRow) {
        const int sourceRow = height - 1 - displayRow;
        for (int col = 0; col < width; ++col) {
            const f32 z = heightmap.at(col, sourceRow);
            const f32 t = std::clamp((z - heightmap.minZ()) / range, 0.0f, 1.0f);

            const u8 red = static_cast<u8>(std::clamp(t * 255.0f, 0.0f, 255.0f));
            const u8 green = static_cast<u8>(std::clamp(t * 240.0f, 0.0f, 255.0f));
            const u8 blue =
                static_cast<u8>(std::clamp((0.3f + t * 0.7f) * 200.0f,
                                           0.0f,
                                           255.0f));
            const auto index =
                static_cast<size_t>((displayRow * width + col) * 4);
            pixels[index + 0] = red;
            pixels[index + 1] = green;
            pixels[index + 2] = blue;
            pixels[index + 3] = 255;
        }
    }

    return pixels;
}

} // namespace carve
} // namespace dw
