#include "image_thumbnail.h"

#include "../loaders/texture_loader.h"
#include "../paths/app_paths.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

#include <algorithm>

namespace dw::image_thumbnail {
namespace {

constexpr int kImportedThumbnailSize = 512;

std::optional<TextureData> fitImageToThumbnail(const TextureData& source) {
    if (source.width <= 0 || source.height <= 0 || source.channels != 4 ||
        source.pixels.size() <
            static_cast<size_t>(source.width) * static_cast<size_t>(source.height) * 4u) {
        return std::nullopt;
    }

    TextureData thumbnail;
    thumbnail.width = kImportedThumbnailSize;
    thumbnail.height = kImportedThumbnailSize;
    thumbnail.channels = 4;
    thumbnail.pixels.assign(static_cast<size_t>(thumbnail.width) *
                                static_cast<size_t>(thumbnail.height) * 4u,
                            0);

    for (size_t i = 0; i < thumbnail.pixels.size(); i += 4) {
        thumbnail.pixels[i + 0] = 42;
        thumbnail.pixels[i + 1] = 45;
        thumbnail.pixels[i + 2] = 50;
        thumbnail.pixels[i + 3] = 255;
    }

    const double scale = std::min(static_cast<double>(thumbnail.width) /
                                      static_cast<double>(source.width),
                                  static_cast<double>(thumbnail.height) /
                                      static_cast<double>(source.height));
    const int dstWidth = std::max(1, std::min(thumbnail.width,
                                             static_cast<int>(source.width * scale + 0.5)));
    const int dstHeight = std::max(1, std::min(thumbnail.height,
                                              static_cast<int>(source.height * scale + 0.5)));
    const int dstX = (thumbnail.width - dstWidth) / 2;
    const int dstY = (thumbnail.height - dstHeight) / 2;

    for (int y = 0; y < dstHeight; ++y) {
        const int srcY = std::min(source.height - 1,
                                  static_cast<int>((static_cast<double>(y) + 0.5) *
                                                   source.height / dstHeight));
        for (int x = 0; x < dstWidth; ++x) {
            const int srcX = std::min(source.width - 1,
                                      static_cast<int>((static_cast<double>(x) + 0.5) *
                                                       source.width / dstWidth));
            const size_t srcOffset =
                (static_cast<size_t>(srcY) * static_cast<size_t>(source.width) +
                 static_cast<size_t>(srcX)) *
                4u;
            const size_t dstOffset =
                (static_cast<size_t>(dstY + y) * static_cast<size_t>(thumbnail.width) +
                 static_cast<size_t>(dstX + x)) *
                4u;
            thumbnail.pixels[dstOffset + 0] = source.pixels[srcOffset + 0];
            thumbnail.pixels[dstOffset + 1] = source.pixels[srcOffset + 1];
            thumbnail.pixels[dstOffset + 2] = source.pixels[srcOffset + 2];
            thumbnail.pixels[dstOffset + 3] = source.pixels[srcOffset + 3];
        }
    }

    return thumbnail;
}

bool writeRgbaAsTga(const Path& path, const TextureData& data) {
    if (data.width <= 0 || data.height <= 0 || data.channels != 4) {
        return false;
    }

    const size_t pixelBytes =
        static_cast<size_t>(data.width) * static_cast<size_t>(data.height) * 4u;
    if (data.pixels.size() < pixelBytes) {
        return false;
    }

    ByteBuffer tga(18u + pixelBytes, 0);
    tga[2] = 2; // Uncompressed true-color
    tga[12] = static_cast<u8>(data.width & 0xFF);
    tga[13] = static_cast<u8>((data.width >> 8) & 0xFF);
    tga[14] = static_cast<u8>(data.height & 0xFF);
    tga[15] = static_cast<u8>((data.height >> 8) & 0xFF);
    tga[16] = 32;   // 32 bits per pixel (BGRA)
    tga[17] = 0x20; // Top-left origin

    for (size_t i = 0; i < pixelBytes; i += 4) {
        const size_t out = 18u + i;
        tga[out + 0] = data.pixels[i + 2];
        tga[out + 1] = data.pixels[i + 1];
        tga[out + 2] = data.pixels[i + 0];
        tga[out + 3] = data.pixels[i + 3];
    }

    return file::writeBinary(path, tga);
}

} // namespace

std::optional<Path> writeCachedTgaFromImage(i64 modelId, const Path& imagePath) {
    if (modelId <= 0) {
        return std::nullopt;
    }

    auto sourceImage = TextureLoader::loadImage(imagePath);
    if (!sourceImage) {
        return std::nullopt;
    }

    auto thumbnailImage = fitImageToThumbnail(*sourceImage);
    if (!thumbnailImage) {
        log::warningf("Library",
                      "Failed to prepare image thumbnail for model %lld from %s",
                      static_cast<long long>(modelId),
                      imagePath.string().c_str());
        return std::nullopt;
    }

    Path thumbnailDir = paths::getThumbnailDir();
    if (!file::exists(thumbnailDir) && !file::createDirectories(thumbnailDir)) {
        log::warning("Library", "Failed to create thumbnail directory");
        return std::nullopt;
    }

    Path thumbnailPath = thumbnailDir / (std::to_string(modelId) + ".tga");
    if (!writeRgbaAsTga(thumbnailPath, *thumbnailImage)) {
        log::warningf("Library",
                      "Failed to write image thumbnail for model %lld to %s",
                      static_cast<long long>(modelId),
                      thumbnailPath.string().c_str());
        return std::nullopt;
    }

    return thumbnailPath;
}

} // namespace dw::image_thumbnail
