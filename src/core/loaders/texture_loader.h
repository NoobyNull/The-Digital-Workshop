#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../types.h"

namespace dw {

// Decoded pixel data from an image (RGBA, 4 channels)
struct TextureData {
    std::vector<uint8_t> pixels; // Raw RGBA pixels (width * height * 4 bytes)
    int width = 0;
    int height = 0;
    int channels = 4; // Always 4 (RGBA) — forced on load
};

// Loads images from disk or memory using stb_image.
// Always decodes to RGBA (4 channels) for consistent GPU upload.
class TextureLoader {
  public:
    // Load a supported image from file path
    // Returns nullopt on failure (logs stbi_failure_reason)
    static std::optional<TextureData> loadImage(const Path& path);

    // Load PNG from file path
    // Returns nullopt on failure (logs stbi_failure_reason)
    static std::optional<TextureData> loadPNG(const Path& path);

    // Load PNG from a memory buffer (e.g. raw bytes from MaterialArchive)
    // Returns nullopt on failure (logs stbi_failure_reason)
    static std::optional<TextureData> loadPNGFromMemory(const uint8_t* data, size_t size);
};

} // namespace dw
