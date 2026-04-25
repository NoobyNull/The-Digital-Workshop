#include "analysis_overlay.h"

#include <algorithm>
#include <cmath>

namespace dw {
namespace carve {

namespace {

// HSV to RGB (h in [0,360), s,v in [0,1])
void hsvToRgb(f32 h, f32 s, f32 v, u8& r, u8& g, u8& b) {
    const f32 c = v * s;
    const f32 x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const f32 m = v - c;
    f32 rf = 0.0f, gf = 0.0f, bf = 0.0f;

    if (h < 60.0f)       { rf = c; gf = x; }
    else if (h < 120.0f) { rf = x; gf = c; }
    else if (h < 180.0f) { gf = c; bf = x; }
    else if (h < 240.0f) { gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; bf = c; }
    else                  { rf = c; bf = x; }

    r = static_cast<u8>((rf + m) * 255.0f);
    g = static_cast<u8>((gf + m) * 255.0f);
    b = static_cast<u8>((bf + m) * 255.0f);
}

} // namespace

std::vector<u8> generateAnalysisOverlay(
    const Heightmap& heightmap,
    const IslandResult& islands,
    const CurvatureResult& curvature,
    int width, int height) {

    if (heightmap.empty() || width <= 0 || height <= 0) {
        return {};
    }

    const auto pixelCount = static_cast<usize>(width * height);
    std::vector<u8> pixels(pixelCount * 4);

    const f32 zMin = heightmap.minZ();
    const f32 zMax = heightmap.maxZ();
    const f32 zRange = (zMax - zMin > 1e-6f) ? (zMax - zMin) : 1.0f;

    const int hmCols = heightmap.cols();
    const int hmRows = heightmap.rows();

    // Map pixel coordinates to heightmap grid
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const auto pixIdx = static_cast<usize>((py * width + px) * 4);

            // Map display Y down to world Y up so the top row is max Y.
            const int hc = (hmCols > 1) ? (px * (hmCols - 1)) / std::max(1, width - 1) : 0;
            const int displayHr =
                (hmRows > 1) ? (py * (hmRows - 1)) / std::max(1, height - 1) : 0;
            const int hr = hmRows - 1 - displayHr;
            const int clampC = std::clamp(hc, 0, hmCols - 1);
            const int clampR = std::clamp(hr, 0, hmRows - 1);

            // Base: grayscale from height
            const f32 z = heightmap.at(clampC, clampR);
            const f32 normalized = (z - zMin) / zRange;
            const auto gray = static_cast<u8>(normalized * 255.0f);

            pixels[pixIdx + 0] = gray;
            pixels[pixIdx + 1] = gray;
            pixels[pixIdx + 2] = gray;
            pixels[pixIdx + 3] = 255;

            // Island overlay: semi-transparent colored fill
            if (islands.maskCols > 0 && islands.maskRows > 0) {
                const int mc = (islands.maskCols > 1)
                    ? (px * (islands.maskCols - 1)) / std::max(1, width - 1) : 0;
                const int displayMr = (islands.maskRows > 1)
                    ? (py * (islands.maskRows - 1)) / std::max(1, height - 1) : 0;
                const int mr = islands.maskRows - 1 - displayMr;
                const int cmc = std::clamp(mc, 0, islands.maskCols - 1);
                const int cmr = std::clamp(mr, 0, islands.maskRows - 1);

                const int islandId = islands.islandMask[
                    static_cast<usize>(cmr * islands.maskCols + cmc)];
                if (islandId >= 0) {
                    // Distinct hue per island (red/orange range: 0-40 degrees, spaced)
                    const f32 hue = std::fmod(
                        static_cast<f32>(islandId) * 37.0f, 60.0f);
                    u8 ir = 0, ig = 0, ib = 0;
                    hsvToRgb(hue, 0.9f, 0.95f, ir, ig, ib);

                    // Alpha blend at 50% opacity
                    pixels[pixIdx + 0] = static_cast<u8>(
                        (static_cast<int>(pixels[pixIdx + 0]) + static_cast<int>(ir)) / 2);
                    pixels[pixIdx + 1] = static_cast<u8>(
                        (static_cast<int>(pixels[pixIdx + 1]) + static_cast<int>(ig)) / 2);
                    pixels[pixIdx + 2] = static_cast<u8>(
                        (static_cast<int>(pixels[pixIdx + 2]) + static_cast<int>(ib)) / 2);
                }
            }
        }
    }

    // Min radius annotation: bright cyan marker at minimum curvature location
    if (curvature.concavePointCount > 0 && hmCols > 0 && hmRows > 0) {
        const int markerPx = (curvature.minRadiusCol * std::max(1, width - 1)) /
                             std::max(1, hmCols - 1);
        const int markerSourcePy =
            (curvature.minRadiusRow * std::max(1, height - 1)) /
            std::max(1, hmRows - 1);
        const int markerPy = height - 1 - markerSourcePy;
        // Draw a 3x3 bright marker
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int mx = markerPx + dx;
                const int my = markerPy + dy;
                if (mx >= 0 && mx < width && my >= 0 && my < height) {
                    const auto idx = static_cast<usize>((my * width + mx) * 4);
                    pixels[idx + 0] = 0;
                    pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255;
                    pixels[idx + 3] = 255;
                }
            }
        }
    }

    return pixels;
}

} // namespace carve
} // namespace dw
