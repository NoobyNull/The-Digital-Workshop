---
phase: 36-critical-rendering-bugs
plan: "02"
subsystem: rendering
tags: [opengl, gl-state, camera, projection, glPointSize, viewport, renderer]

requires:
  - phase: 35-gcodepanel-rendering-elimination
    provides: ViewportPanel with renderGCodeLines and simulation cutter dot rendering

provides:
  - Stale projection matrix fix: renderer receives updated camera after far plane restore (RBUG-01)
  - glPointSize state cleanup in Renderer::renderPoint (RBUG-03)
  - glPointSize state cleanup in ViewportPanel::renderGCodeLines cutter dot (RBUG-04)

affects: [rendering, viewport, renderer, simulation-playback]

tech-stack:
  added: []
  patterns:
    - "GL state restore: always reset modified GL state (glPointSize, glLineWidth, etc.) before returning from render functions"
    - "Camera propagation: after mutating camera state, always call m_renderer.setCamera(m_camera) to push new projection matrix"

key-files:
  created: []
  modified:
    - src/ui/panels/viewport_panel.cpp
    - src/render/renderer.cpp

key-decisions:
  - "glPointSize(1.0f) reset placed after glEnable(GL_DEPTH_TEST) in renderPoint() for consistent state on exit"
  - "m_renderer.setCamera(m_camera) added inside the if-block (not unconditionally) to mirror the existing pattern for the extension case"

patterns-established:
  - "GL state restore pattern: each render helper that modifies GL state must restore it before returning"

requirements-completed: [RBUG-01, RBUG-03, RBUG-04]

duration: 10min
completed: 2026-03-28
---

# Phase 36 Plan 02: Critical Rendering Bugs (Point Size + Projection Matrix) Summary

**Three one-line GL state fixes: projection matrix re-sent after far plane restore, glPointSize(1.0f) restored in renderPoint() and renderGCodeLines() cutter dot.**

## Performance

- **Duration:** ~10 min
- **Started:** 2026-03-28T17:55:00Z
- **Completed:** 2026-03-28T18:05:00Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- RBUG-01: After the far plane is restored to its saved value, `m_renderer.setCamera(m_camera)` is now called to push the corrected projection matrix -- eliminates the one-frame stale projection glitch
- RBUG-03: `Renderer::renderPoint()` now calls `glPointSize(1.0f)` before returning, preventing point size contamination of subsequent draw calls
- RBUG-04: `ViewportPanel::renderGCodeLines()` cutter dot block now resets `glPointSize(1.0f)` after the 8px red dot is drawn

## Task Commits

1. **Task 1: Fix stale projection after far plane restore (RBUG-01)** - `e013c65` (fix)
2. **Task 2: Fix glPointSize leaks in renderPoint and renderGCodeLines (RBUG-03, RBUG-04)** - `2212f8f` (fix)

## Files Created/Modified

- `src/ui/panels/viewport_panel.cpp` - Added `m_renderer.setCamera(m_camera)` after far plane restore; added `glPointSize(1.0f)` after cutter dot draw
- `src/render/renderer.cpp` - Added `glPointSize(1.0f)` at end of `renderPoint()` before closing brace

## Decisions Made

- `glPointSize(1.0f)` placed after `glEnable(GL_DEPTH_TEST)` in `renderPoint()` (not before) to maintain logical ordering: draw, then restore depth test, then restore point size
- `m_renderer.setCamera(m_camera)` kept inside the existing `if (m_camera.farPlane() != savedFar)` block rather than added unconditionally, mirroring the guard pattern used for the far plane extension case

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None. Build directory was in `/data/DW/build` (not in worktree), build succeeded without issues, all 936 tests passed.

## Next Phase Readiness

- RBUG-01, RBUG-03, RBUG-04 all resolved
- Remaining phase 36 work (RBUG-02: framebuffer viewport not restored; coordinate swap consolidation; RAII GL state management; CNC connection dedup) can proceed

---
*Phase: 36-critical-rendering-bugs*
*Completed: 2026-03-28*
