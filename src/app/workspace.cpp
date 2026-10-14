// Digital Workshop - Workspace Implementation

#include "app/workspace.h"

#include "core/utils/thread_utils.h"

namespace dw {

void Workspace::setFocusedMesh(std::shared_ptr<Mesh> mesh) {
    ASSERT_MAIN_THREAD();
    m_focusedMesh = std::move(mesh);
}

void Workspace::clearFocusedMesh() {
    ASSERT_MAIN_THREAD();
    m_focusedMesh.reset();
}

void Workspace::setFocusedGCode(std::shared_ptr<GCodeFile> gcode) {
    ASSERT_MAIN_THREAD();
    m_focusedGCode = std::move(gcode);
}

void Workspace::clearFocusedGCode() {
    ASSERT_MAIN_THREAD();
    m_focusedGCode.reset();
}

void Workspace::setFocusedToolpath(std::shared_ptr<Mesh> toolpathMesh) {
    ASSERT_MAIN_THREAD();
    m_focusedToolpath = std::move(toolpathMesh);
}

void Workspace::clearFocusedToolpath() {
    ASSERT_MAIN_THREAD();
    m_focusedToolpath.reset();
}

void Workspace::clearAll() {
    ASSERT_MAIN_THREAD();
    m_focusedMesh.reset();
    m_focusedGCode.reset();
    m_focusedToolpath.reset();
}

} // namespace dw
