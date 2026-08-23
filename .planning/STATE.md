---
gsd_state_version: 1.0
milestone: v0.8.0
milestone_name: PureCutCNC CAM Integration
status: phase-2-complete
stopped_at: Phase 2 (Sidecar) complete; Phase 3 (bridge session API) next
last_updated: "2026-08-23T00:00:00-07:00"
last_activity: 2026-08-23
progress:
  total_phases: 7
  completed_phases: 2
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

- **Phase:** 2 of 7 — Sidecar (complete)
- **Next phase:** 3 — Bridge session API

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

## Phase 2 (Sidecar) — complete

The biggest identified risk (Bun-compile of `manifold-3d`) resolved negative:
single-file `bun build --compile` is not viable — the bundler breaks
manifold's emscripten glue, and a compiled binary cannot resolve external
packages at runtime. The sidecar instead ships as the Bun runtime plus a
bundled `dw-cam-engine.js` (`bun build --target=bun --external manifold-3d`)
plus `node_modules/manifold-3d` alongside it: ~103 MB payload on disk, ~98 MB
compressed into the Linux `.run` installer (171 MB staged total).
`packaging/build-cam-engine.sh` builds the payload; `packaging/smoke-cam-engine.sh`
smokes it end to end (health check, `/api/machines`, a real toolpath job).

C++ side: `dw::cam::CamEngineClient` is a pure, never-throw response parser
(adversarially verified and regression-tested), and `dw::cam::CamEngineRuntime`
mirrors the existing `OllamaRuntime` pattern — fork/exec with cwd set to the
payload directory, typed status reporting, SIGTERM-then-SIGKILL teardown.
Unlike the template, a dead child is reaped with a WNOHANG waitpid before
spawning, so a crashed engine restarts on the next start request (verified
live: SIGKILL the child, `ensureReady()` respawns and reports ready; the
same stale-pid flaw still exists in `OllamaRuntime` and is noted for a
future pass). Windows process management is left unmanaged, matching the
template's current scope. In the app, the runtime is lazily constructed on the CAM
placeholder panel's "Start engine" button (synchronous `ensureReady()` for
now; Phase 3 replaces this with the async bridge session API), and torn down
in `Application::shutdown()`.

Linux packaging installs the payload to
`$PREFIX/share/digitalworkshop/resources/cam-engine`, where it's found by
the existing bundled-resource fallback path with zero C++ changes. Full
package smoke is green, including a real job run through the installed
payload.

**Known gaps (recorded honestly, not yet closed):**
- The TGZ/CPack package does **not** ship the cam-engine payload — it's
  staged through a separate CMake `install()` mechanism than the `.run`
  installer uses. This needs a design decision, targeted at the
  packaging/CI phase, not Phase 3.
- Windows/macOS payload staging is deferred to CI; `packaging/build-cam-engine.sh`
  has a `--platform` flag stubbed for this but unimplemented.
- The GUI "Start engine" button click itself is manually verifiable only —
  no automated UI-level test exercises it.

## Next Action

Start Phase 3 (bridge session API): the full CAMJ surface exposed as bridge
session endpoints, a parity checklist derived against the existing web UI,
a schema-export endpoint to drive generated parameter forms, and contract
tests against the sidecar.

Carried into Phase 3 from the Phase 2 final review: expose
`paths::getExeDir()` in `app_paths.h` instead of the copied `/proc/self/exe`
idiom in `application_wiring_cnc.cpp`; move the generic HTTP helpers out of
the `dw::lmstudio` namespace once the session client widens their use;
hoist the hardcoded 10 s reachability wait into `CamEngineConfig` if any
synchronous path survives; revisit `curlPost`'s 120 s timeout before real
jobs stream through the client.

---
*v0.8.0 Phase 1 (Yank) completed: 2026-08-23*
*v0.8.0 Phase 2 (Sidecar) completed: 2026-08-23*
