#pragma once

// Digital Workshop - Workspace
// Central state manager for the application's "focused object" design.
// Panels observe and act on the currently focused model or g-code.

#include <memory>

namespace dw {

// Forward declarations - these will be implemented in core modules
class Mesh;
class GCodeFile;

class Workspace {
  public:
    Workspace() = default;
    ~Workspace() = default;

    // Focused model (3D mesh)
    void setFocusedMesh(std::shared_ptr<Mesh> mesh);
    auto getFocusedMesh() const -> std::shared_ptr<Mesh> { return m_focusedMesh; }
    auto hasFocusedMesh() const -> bool { return m_focusedMesh != nullptr; }
    void clearFocusedMesh();

    // Focused G-code file
    void setFocusedGCode(std::shared_ptr<GCodeFile> gcode);
    auto getFocusedGCode() const -> std::shared_ptr<GCodeFile> { return m_focusedGCode; }
    auto hasFocusedGCode() const -> bool { return m_focusedGCode != nullptr; }
    void clearFocusedGCode();

    // Focused toolpath mesh (generated from G-code)
    void setFocusedToolpath(std::shared_ptr<Mesh> toolpathMesh);
    auto getFocusedToolpath() const -> std::shared_ptr<Mesh> { return m_focusedToolpath; }
    auto hasFocusedToolpath() const -> bool { return m_focusedToolpath != nullptr; }
    void clearFocusedToolpath();

    // Clear all focused objects
    void clearAll();

  private:
    std::shared_ptr<Mesh> m_focusedMesh;
    std::shared_ptr<GCodeFile> m_focusedGCode;
    std::shared_ptr<Mesh> m_focusedToolpath;
};

} // namespace dw
