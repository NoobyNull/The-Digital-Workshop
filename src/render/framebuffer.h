#pragma once

#include <glad/gl.h>

#include "../core/types.h"

namespace dw {

// Offscreen framebuffer for rendering to texture
class Framebuffer {
  public:
    Framebuffer() = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    // Create framebuffer with given dimensions
    bool create(int width, int height);
    void destroy();

    // Resize (destroys and recreates)
    bool resize(int width, int height);

    // Bind/unbind
    void bind();
    void unbind();

    // Access
    bool isValid() const { return m_fbo != 0; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    GLuint handle() const { return m_fbo; }
    GLuint colorTexture() const { return m_colorTexture; }
    GLuint depthTexture() const { return m_depthTexture; }

    // Read pixels
    ByteBuffer readPixels() const;

  private:
    GLuint m_fbo = 0;
    GLuint m_colorTexture = 0;
    GLuint m_depthTexture = 0;
    int m_width = 0;
    int m_height = 0;
    GLint m_savedViewport[4] = {0, 0, 0, 0};
};

} // namespace dw
