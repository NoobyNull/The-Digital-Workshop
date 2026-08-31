#include "library_panel.h"

#include <cstdint>
#include <fstream>
#include <vector>

#include "../../core/loaders/texture_loader.h"
#include "../../core/paths/app_paths.h"
#include "../../core/utils/log.h"

namespace dw {

GLuint LibraryPanel::getPlaceholderTexture() {
    if (m_placeholderLoaded)
        return m_placeholderTexture;
    m_placeholderLoaded = true;

    const Path iconPath = paths::getBundledIconsDir() / "statue.png";
    const auto data = TextureLoader::loadPNG(iconPath);
    if (!data) {
        log::warning("Library", "Failed to load placeholder icon: statue.png");
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 data->width,
                 data->height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 data->pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_placeholderTexture = texture;
    return texture;
}

void LibraryPanel::clearTextureCache() {
    for (auto& [id, texture] : m_textureCache) {
        (void)id;
        if (texture != 0)
            glDeleteTextures(1, &texture);
    }
    m_textureCache.clear();
}

GLuint LibraryPanel::loadTGATexture(const Path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        log::warningf("Library", "Failed to open TGA file: %s", path.string().c_str());
        return 0;
    }

    std::uint8_t header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file) {
        log::warningf("Library", "Failed to read TGA header: %s", path.string().c_str());
        return 0;
    }
    if (header[2] != 2 || header[16] != 32) {
        log::warningf("Library",
                      "Unsupported TGA format (type=%d, bpp=%d): %s",
                      header[2],
                      header[16],
                      path.string().c_str());
        return 0;
    }

    const int width = header[12] | (header[13] << 8);
    const int height = header[14] | (header[15] << 8);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        log::warningf("Library",
                      "Invalid TGA dimensions (%dx%d): %s",
                      width,
                      height,
                      path.string().c_str());
        return 0;
    }

    const std::size_t dataSize =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    std::vector<std::uint8_t> bgra(dataSize);
    file.read(reinterpret_cast<char*>(bgra.data()), static_cast<std::streamsize>(dataSize));
    if (!file) {
        log::warningf("Library", "Failed to read TGA pixel data: %s", path.string().c_str());
        return 0;
    }
    for (std::size_t index = 0; index < dataSize; index += 4)
        std::swap(bgra[index], bgra[index + 2]);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        log::warning("Library", "Failed to create GL texture for thumbnail");
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 bgra.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint LibraryPanel::getThumbnailTexture(const ModelRecord& model) {
    const auto cached = m_textureCache.find(model.id);
    if (cached != m_textureCache.end())
        return cached->second;
    if (model.thumbnailPath.empty())
        return 0;

    const GLuint texture = loadTGATexture(model.thumbnailPath);
    m_textureCache[model.id] = texture;
    return texture;
}

GLuint LibraryPanel::getThumbnailTextureForModel(int64_t modelId) const {
    const auto cached = m_textureCache.find(modelId);
    return cached != m_textureCache.end() ? cached->second : 0;
}

void LibraryPanel::invalidateThumbnail(int64_t modelId) {
    const auto cached = m_textureCache.find(modelId);
    if (cached == m_textureCache.end())
        return;
    if (cached->second != 0)
        glDeleteTextures(1, &cached->second);
    m_textureCache.erase(cached);
}

} // namespace dw
