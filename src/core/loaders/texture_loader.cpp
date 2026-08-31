// STB_IMAGE_IMPLEMENTATION must be defined in exactly ONE .cpp file.
// It is defined here so that stb_image.h compiles its implementation once.
#define STB_IMAGE_IMPLEMENTATION
#include "texture_loader.h"

#include <stb_image.h>

#include "../utils/log.h"

namespace dw {

std::optional<TextureData> TextureLoader::loadImage(const Path& path) {
    int width = 0;
    int height = 0;
    int srcChannels = 0;

    // Force RGBA (4 channels) regardless of source format
    unsigned char* raw =
        stbi_load(path.string().c_str(), &width, &height, &srcChannels, STBI_rgb_alpha);
    if (!raw) {
        log::errorf("TextureLoader",
                    "Failed to load image '%s': %s",
                    path.string().c_str(),
                    stbi_failure_reason());
        return std::nullopt;
    }

    TextureData data;
    data.width = width;
    data.height = height;
    data.channels = 4;
    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    data.pixels.assign(raw, raw + byteCount);
    stbi_image_free(raw);

    return data;
}

std::optional<TextureData> TextureLoader::loadPNG(const Path& path) {
    return loadImage(path);
}

std::optional<TextureData> TextureLoader::loadPNGFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        log::error("TextureLoader", "loadPNGFromMemory called with null or empty buffer");
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int srcChannels = 0;

    // Force RGBA (4 channels)
    unsigned char* raw = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data),
                                               static_cast<int>(size),
                                               &width,
                                               &height,
                                               &srcChannels,
                                               STBI_rgb_alpha);

    if (!raw) {
        log::errorf("TextureLoader", "Failed to decode PNG from memory: %s", stbi_failure_reason());
        return std::nullopt;
    }

    TextureData result;
    result.width = width;
    result.height = height;
    result.channels = 4;
    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    result.pixels.assign(raw, raw + byteCount);
    stbi_image_free(raw);

    return result;
}

} // namespace dw
