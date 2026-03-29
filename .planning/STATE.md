---
gsd_state_version: 1.0
milestone: v0.5.5
milestone_name: Unified 3D Viewport
status: executing
stopped_at: Completed 39-01-PLAN.md (CNC controller dedup)
last_updated: "2026-03-29T01:42:05.392Z"
last_activity: 2026-03-29
progress:
  total_phases: 25
  completed_phases: 19
  total_plans: 44
  completed_plans: 42
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-28)

**Core value:** A woodworker can go from selecting a piece of wood and a cutting tool to safely running a CNC job with optimized feeds and speeds -- all without leaving the application.
**Current focus:** Phase 36 — critical-rendering-bugs

## Current Position

Phase: 36
Plan: Not started
Status: Executing Phase 36 (36-01 complete)
Last activity: 2026-03-29

Progress: [----------] 0/4 phases complete

## Accumulated Context

### Decisions

- Unified viewport (Option B) -- merge all 3D rendering into ViewportPanel
- FitParams as alignment source of truth for model-gcode overlay
- Phase numbering continues from 35 (v0.5.5 ended at 35)
- No research phase -- internal refactoring/bugfix work
- Coordinate swap centralization via inline helper function (not matrix-based)
- GL state management via RAII guard pattern (GLStateScope in renderer.cpp)
- CncController dedup via private initializeConnection() helper (not base class refactor)
- Phase 36 must execute before phases 37/38 (bug fixes are the baseline for refactors)
- Phases 37, 38, and 39 are independent of each other once Phase 36 is complete
- m_savedViewport zeroed on moved-from Framebuffer objects since viewport state is only valid between bind/unbind
- [Phase 39]: CncController dedup via private initializeConnection() helper -- not base class refactor

### Known Fix Locations (from audit)

- RBUG-01: viewport_panel.cpp:707 -- add m_renderer.setCamera(m_camera) after far plane restore
- RBUG-02: framebuffer.cpp -- unbind() must save/restore previous GL viewport rect
- RBUG-03: renderer.cpp:361 -- renderPoint() must call glPointSize(1.0f) before return
- RBUG-04: viewport_panel.cpp:1601 -- renderGCodeLines() must call glPointSize(1.0f) before return
- COORD-01/02: 5 swap sites at viewport_panel.cpp lines 632, 694-698, 1057-1058, 1543-1548, 1562-1566
- GLST-01/02: 5 render functions (renderMesh, renderToolpath, renderGrid, renderAxis, renderWireBox)
- CNC-01: connect() and connectTcp() share ~8 lines of init code

### Pending Todos

None.

### Blockers/Concerns

None.

## Session Continuity

Last session: 2026-03-29T01:42:05.390Z
Stopped at: Completed 39-01-PLAN.md (CNC controller dedup)
Resume file: None
Next action: Execute Phase 36 Plan 02

---
*State initialized: 2026-02-27*
*Last updated: 2026-03-29 -- 36-01 completed (RBUG-02 framebuffer viewport fix)*
