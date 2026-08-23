---
gsd_state_version: 1.0
milestone: v0.8.0
milestone_name: PureCutCNC CAM Integration
status: phase-1-complete
stopped_at: Phase 1 (Yank) complete; Phase 2 (sidecar packaging) next
last_updated: "2026-08-23T00:00:00-07:00"
last_activity: 2026-08-23
progress:
  total_phases: 7
  completed_phases: 1
---

# Project State

## Previous milestone

v0.7.0 (Project-Centered Workshop) is frozen as an engineering candidate at
`d8c4763`. Its human-study release gate (the five-person novice River Sign
protocol) was retired without being run: the guided carve flow it was meant
to validate was removed as part of the v0.8.0 CAM rebuild described below.

## Current Focus

- **Milestone:** v0.8.0 PureCutCNC CAM Integration
- **Design:** `.planning/CAM-INTEGRATION-DESIGN.md`
- **Phase 1 plan:** `.planning/plans/2026-08-23-cam-phase1-yank.md`
- **Requirements:** `.planning/REQUIREMENTS.md`
- **Roadmap:** `.planning/ROADMAP.md`

## Core Outcome

Digital Workshop's internal CAM (Direct Carve toolpath generation) is
replaced by the PureCutCNC engine, integrated as a sidecar process behind a
fully native Dear ImGui "CAM" workspace — no webview, no browser UI. DW's
GRBL sender, protected Run boundary, tool database, and machine profiles are
unchanged; the engine only produces G-code for DW to stream.

## Current Position

- **Phase:** 1 of 7 — Yank (complete)
- **Next phase:** 2 — Sidecar packaging

Phase 1 removed Digital Workshop's internal CAM outright, in dependency
order, keeping the build and full suite green at every step:

- Deleted the dead-in-production CAM leaves (4 analysis/recommendation
  units with no remaining callers).
- Cut the application over: `CamPlaceholderPanel` now owns the `direct_carve`
  window ID (retitled "CAM"), and the UX-capture and River Sign study
  machinery tied to the old guided flow were removed.
- Deleted the Direct Carve panel family (26 files) and the Direct Carve CAM
  core (32 files), while keeping `model_fitter` and `alignment_validator`
  for the viewport, which do not belong to CAM.
- Hardened the Run boundary ahead of the core deletion:
  `DirectCarveRunEffectAdapter` now streams the fingerprint-verified
  persisted G-code file directly via `CncController::startStream`
  (`CarveJob` is gone), and run modal-state scanning uses the same filtered
  lines that were actually streamed.
- Kept, untouched, for the Phase 2+ rebuild: the `dw_carve_preparation`
  module and `carve_preparation_adapter` (still live in resume/workshop
  wiring), `RunCoordinator`/`RunPackage`, the GRBL controller, the tool
  database, materials, and the viewport.
- Pruned `cmake/SourceSizeCaps.cmake` of one dead entry
  (`src/core/carve/toolpath_generator.cpp`, deleted with the CAM core) and
  ratcheted `gcode_panel.cpp`'s cap down to its current (shrunk) size; the
  source-size policy passes across all remaining production files.
- Full suite: 1,485 tests passing, 2 env-gated skips, zero failures.
  Sequential `ctest` is 100% pass (1,710 run / 2 skipped, including both
  `river_sign_study` script tests, which still pass standalone against the
  scripts on disk). Diagnostic-mode launch (`--diagnostic`) initializes
  cleanly end to end, including the CNC simulator.

## Next Action

Start Phase 2 (sidecar packaging). Per the design doc's Risks section, the
first task is a Bun-compile proof of concept for `dw-bridge` (the API layer
that runs inside the `dw-cam-engine` sidecar binary), specifically
validating that the wasm dependency (`manifold-3d`) compiles cleanly with
`bun build --compile` — this is the biggest identified risk to the whole
milestone and is called out to be attempted early. Fallbacks if Bun compile
fails: a Node SEA bundle, then shipping a minimal Node runtime alongside the
app.

---
*v0.8.0 Phase 1 (Yank) completed: 2026-08-23*
