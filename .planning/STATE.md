---
gsd_state_version: 1.0
milestone: v0.5.5
milestone_name: Unified 3D Viewport
status: verifying
stopped_at: Completed 36-02-PLAN.md (glPointSize leaks + stale projection fix)
last_updated: "2026-03-29T00:57:43.173Z"
last_activity: 2026-03-29
progress:
  total_phases: 10
  completed_phases: 0
  total_plans: 7
  completed_plans: 13
  percent: 90
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-09)

**Core value:** A woodworker can go from selecting a piece of wood and a cutting tool to safely running a CNC job with optimized feeds and speeds -- all without leaving the application.
**Current focus:** Milestone v0.5.5 Unified 3D Viewport -- Phase 34 complete, Phase 35 next

## Current Position

Phase: 34 of 35 (Simulation Playback)
Plan: 1 of 1
Status: Phase complete — ready for verification
Last activity: 2026-03-29

Progress: [########=.] 90%

## Accumulated Context

### Decisions

- Unified viewport (Option B) -- merge all 3D rendering into ViewportPanel
- FitParams as alignment source of truth for model-gcode overlay
- Point-match validation (1% sample) confirms alignment, doesn't solve it
- GCodePanel loses 3D rendering, keeps control/info role
- External .nc files render in viewport, no model required
- Phase numbering continues from 31 (v0.5.0 ended at 30)
- No research phase -- internal architectural work
- Height-line shader owned by Renderer (not per-panel) for shared access
- No filter toggles in Phase 31 -- all move types render unconditionally
- G-code lines are separate rendering layer from existing toolpath mesh
- Callback wiring in application_wiring_cnc.cpp alongside existing GCodePanel setup
- Null-check on viewportPanel() pointer for safety against panel ordering changes
- Reused GCodePanel's 8-color toolColor palette and ToolGroup pattern for consistency
- Z-clip filtering in G-code space (not renderer Y-up space) matching GCodePanel convention
- Move-type filtering at geometry build time for GPU efficiency
- Direct matrix construction for FitParams (single swapYZ * fitMat multiply, no chain)
- Fire FitParams callback every frame in renderModelFit() -- matrix update is cheap
- Brute-force point-to-triangle distance with 1% stride sampling for alignment validation
- 70% near-ratio threshold for Aligned status (accounts for approach/retract segments)
- Deterministic stride-based sampling for reproducible validation results
- VPSimState enum separate from GCodePanel SimState to avoid viewport-gcode_panel header coupling
- Scrubbing auto-transitions Stopped->Paused so slider drag shows live overlay
- Statistics computed via gcode::Analyzer in wiring callback for independent viewport simulation
- [Phase 36-critical-rendering-bugs]: GL state restore: each render helper that modifies GL state must restore it before returning (established by 36-02 glPointSize fixes)
- [Phase 36-critical-rendering-bugs]: Camera propagation: after mutating camera state with setFarPlane, call m_renderer.setCamera(m_camera) to push new projection matrix

### Pending Todos

None.

### Blockers/Concerns

None.

## Session Continuity

Last session: 2026-03-29T00:57:43.171Z
Stopped at: Completed 36-02-PLAN.md (glPointSize leaks + stale projection fix)
Resume file: None
Next action: Execute Phase 35

---
*State initialized: 2026-02-27*
*Last updated: 2026-03-09 -- 34-01 completed (Simulation Playback)*
