#pragma once

#include "../core/mesh/mesh.h"
#include "../core/types.h"

namespace dw {

class Texture;

// Thumbnail generation settings
struct ThumbnailSettings {
    int width = 512;
    int height = 512;
    Color backgroundColor{0.2f, 0.2f, 0.2f, 1.0f};
    Color objectColor = Color::fromHex(0x6699CC);
    bool showGrid = false;
    const Texture* materialTexture = nullptr;
    float cameraPitch = 30.0f;
    float cameraYaw = 45.0f;
    float viewportFill = 0.8f;
};

// Generates thumbnail images for 3D models
class ThumbnailGenerator {
  public:
    ThumbnailGenerator() = default;
    ~ThumbnailGenerator();

    // Initialize OpenGL resources
    bool initialize();

    // Shutdown and release resources
    void shutdown();

    // Generate thumbnail and save to file
    // Returns true if successful
    bool generate(const Mesh& mesh,
                  const Path& outputPath,
                  const ThumbnailSettings& settings = ThumbnailSettings{});

    // Generate thumbnail to memory buffer (RGBA pixels)
    ByteBuffer generateToBuffer(const Mesh& mesh,
                                const ThumbnailSettings& settings = ThumbnailSettings{});

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

  private:
    bool m_initialized = false;
};

} // namespace dw
