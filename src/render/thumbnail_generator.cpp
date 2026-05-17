#include "thumbnail_generator.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include <glad/gl.h>

#include "../core/utils/file_utils.h"
#include "../core/utils/log.h"
#include "../core/mesh/mesh_preview.h"
#include "camera.h"
#include "framebuffer.h"
#include "renderer.h"

namespace dw {

namespace {

constexpr u32 kMaxThumbnailTriangles = 4000000;

// Simple TGA writer for thumbnails (no external dependencies)
bool writeTGA(const Path& path, const ByteBuffer& pixels, int width, int height) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    // TGA header (18 bytes)
    uint8_t header[18] = {0};
    header[2] = 2; // Uncompressed RGB
    header[12] = static_cast<uint8_t>(width & 0xFF);
    header[13] = static_cast<uint8_t>((width >> 8) & 0xFF);
    header[14] = static_cast<uint8_t>(height & 0xFF);
    header[15] = static_cast<uint8_t>((height >> 8) & 0xFF);
    header[16] = 32;   // 32 bits per pixel (BGRA)
    header[17] = 0x20; // Top-left origin

    file.write(reinterpret_cast<const char*>(header), 18);

    // Convert RGBA to BGRA and flip rows (glReadPixels is bottom-up, TGA needs top-down)
    size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> bgra(pixels.size());
    for (int y = 0; y < height; y++) {
        size_t srcRow = static_cast<size_t>(height - 1 - y) * rowBytes;
        size_t dstRow = static_cast<size_t>(y) * rowBytes;
        for (size_t x = 0; x < rowBytes; x += 4) {
            bgra[dstRow + x + 0] = pixels[srcRow + x + 2]; // B
            bgra[dstRow + x + 1] = pixels[srcRow + x + 1]; // G
            bgra[dstRow + x + 2] = pixels[srcRow + x + 0]; // R
            bgra[dstRow + x + 3] = pixels[srcRow + x + 3]; // A
        }
    }

    file.write(reinterpret_cast<const char*>(bgra.data()),
               static_cast<std::streamsize>(bgra.size()));

    return file.good();
}

} // namespace

ThumbnailGenerator::~ThumbnailGenerator() {
    shutdown();
}

bool ThumbnailGenerator::initialize() {
    if (m_initialized) {
        return true;
    }

    m_initialized = true;
    return true;
}

void ThumbnailGenerator::shutdown() {
    m_initialized = false;
}

bool ThumbnailGenerator::generate(const Mesh& mesh,
                                  const Path& outputPath,
                                  const ThumbnailSettings& settings) {
    auto pixels = generateToBuffer(mesh, settings);
    if (pixels.empty()) {
        return false;
    }

    // Use TGA format (simple to write, no dependencies)
    Path tgaPath = outputPath;
    if (file::getExtension(outputPath) != "tga") {
        tgaPath = outputPath.string() + ".tga";
    }

    if (!writeTGA(tgaPath, pixels, settings.width, settings.height)) {
        log::errorf("Thumbnail", "Failed to write: %s", tgaPath.string().c_str());
        return false;
    }

    return true;
}

ByteBuffer ThumbnailGenerator::generateToBuffer(const Mesh& mesh,
                                                const ThumbnailSettings& settings) {
    if (mesh.vertexCount() == 0) {
        return {};
    }

    const Mesh* renderMesh = &mesh;
    Mesh previewMesh;
    if (mesh.triangleCount() > kMaxThumbnailTriangles) {
        log::infof("Thumbnail",
                   "Sampling oversized mesh for preview: %u -> %u triangles",
                   mesh.triangleCount(),
                   kMaxThumbnailTriangles);
        previewMesh = mesh_preview::sampleTrianglesForPreview(mesh, kMaxThumbnailTriangles);
        if (!previewMesh.isValid()) {
            log::error("Thumbnail", "Failed to build sampled preview mesh");
            return {};
        }
        renderMesh = &previewMesh;
    }

    // Create framebuffer
    Framebuffer fb;
    if (!fb.create(settings.width, settings.height)) {
        log::error("Thumbnail", "Failed to create framebuffer");
        return {};
    }

    // Create temporary renderer
    Renderer renderer;
    if (!renderer.initialize()) {
        log::error("Thumbnail", "Failed to initialize renderer");
        return {};
    }

    renderer.settings().objectColor = settings.objectColor;
    renderer.settings().showGrid = settings.showGrid;
    renderer.settings().showAxis = false;

    // Setup camera to fit the model
    Camera camera;
    camera.setViewport(settings.width, settings.height);
    camera.setPitch(settings.cameraPitch);
    camera.setYaw(settings.cameraYaw);

    // Fit projected bounds so long or flat models occupy roughly 80% of the thumbnail.
    const auto& bounds = renderMesh->bounds();
    camera.fitToBoundsProjected(bounds.min, bounds.max, settings.viewportFill);

    // Render to framebuffer
    fb.bind();

    renderer.setCamera(camera);
    renderer.beginFrame(settings.backgroundColor);
    renderer.renderMesh(*renderMesh, settings.materialTexture);
    renderer.endFrame();

    // Read pixels
    ByteBuffer pixels = fb.readPixels();

    fb.unbind();

    // Cleanup
    renderer.shutdown();

    return pixels;
}

} // namespace dw
