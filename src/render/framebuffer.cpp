#include "framebuffer.h"

#include "../core/utils/log.h"
#include "gl_utils.h"

namespace dw {

Framebuffer::~Framebuffer() {
    destroy();
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_fbo(other.m_fbo), m_colorTexture(other.m_colorTexture),
      m_depthTexture(other.m_depthTexture), m_width(other.m_width), m_height(other.m_height),
      m_savedViewport{other.m_savedViewport[0], other.m_savedViewport[1],
                      other.m_savedViewport[2], other.m_savedViewport[3]} {
    other.m_fbo = 0;
    other.m_colorTexture = 0;
    other.m_depthTexture = 0;
    other.m_width = 0;
    other.m_height = 0;
    other.m_savedViewport[0] = 0;
    other.m_savedViewport[1] = 0;
    other.m_savedViewport[2] = 0;
    other.m_savedViewport[3] = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_fbo = other.m_fbo;
        m_colorTexture = other.m_colorTexture;
        m_depthTexture = other.m_depthTexture;
        m_width = other.m_width;
        m_height = other.m_height;
        m_savedViewport[0] = other.m_savedViewport[0];
        m_savedViewport[1] = other.m_savedViewport[1];
        m_savedViewport[2] = other.m_savedViewport[2];
        m_savedViewport[3] = other.m_savedViewport[3];
        other.m_fbo = 0;
        other.m_colorTexture = 0;
        other.m_depthTexture = 0;
        other.m_width = 0;
        other.m_height = 0;
        other.m_savedViewport[0] = 0;
        other.m_savedViewport[1] = 0;
        other.m_savedViewport[2] = 0;
        other.m_savedViewport[3] = 0;
    }
    return *this;
}

bool Framebuffer::create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    m_width = width;
    m_height = height;

    // Create framebuffer
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Create color texture
    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    // Create depth texture
    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT24,
                 width,
                 height,
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

    // Check completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::errorf("Framebuffer", "Not complete: %d", status);
        destroy();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Framebuffer::destroy() {
    if (m_colorTexture != 0) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_depthTexture != 0) {
        glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    m_width = 0;
    m_height = 0;
}

bool Framebuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) {
        return true;
    }
    destroy();
    return create(width, height);
}

void Framebuffer::bind() {
    glGetIntegerv(GL_VIEWPORT, m_savedViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
}

ByteBuffer Framebuffer::readPixels() const {
    ByteBuffer buffer(static_cast<usize>(m_width * m_height * 4));

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return buffer;
}

} // namespace dw
