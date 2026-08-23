# CAM Integration Design — PureCutCNC as DW's native CAM

- **Milestone:** v0.8.0 (new milestone; v0.7.0 candidate frozen at `d8c4763`)
- **Date:** 2026-08-23
- **Approach:** Big-bang replacement (approved): yank Direct Carve CAM first, rebuild on the PureCutCNC engine
- **Engine pin:** PureCutCNC v0.3.0 (`9011ee0`), git submodule at `cambridge/vendor/purecutcnc`

## Goal

Remove Digital Workshop's internal CAM (Direct Carve toolpath generation) and
integrate the PureCutCNC engine as the sole CAM backend, with a fully native
Dear ImGui interface. No webview, no browser UI anywhere in the app. Every
capability the PureCutCNC web interface offers must be available in the new
native "CAM" workspace.

## Non-goals

- No changes to DW's GRBL sender or the protected Run boundary — the engine
  produces G-code; DW streams it.
- No adoption of PureCutCNC's sender, UI framework, or Tauri shell.
- No abandonment of DW's Vectric-compatible tool database or machine profiles.

## Architecture

Two processes, one native UI:

```
digital_workshop (C++/ImGui)                    dw-cam-engine (sidecar binary)
+------------------------------+  localhost     +--------------------------+
| CAM workspace (native panels)|  JSON/HTTP     | dw-bridge (grown API)    |
| CamEngineClient (libcurl)    | <-----------> | PureCutCNC engine v0.3.0 |
| CamInterpreter (tools/mach.) |                | (pristine submodule)     |
| CAMJ document (authoritative)|                +--------------------------+
| Viewport, RunCoordinator,    |
| GRBL sender - all unchanged  |
+------------------------------+
```

Key decisions:

1. **Sidecar, not embedded.** The engine is TypeScript; it ships as a
   three-part payload proven by the Phase 2 proof of concept: the Bun
   runtime binary, a bundled `dw-cam-engine.js`
   (`bun build --target=bun --external manifold-3d`, ~5 MB), and
   `node_modules/manifold-3d` beside it (kept external because Bun's
   bundler breaks the emscripten glue, and `--compile` binaries cannot
   resolve external packages — both verified). ~103 MB total. Development
   mode runs `bun bridge/server.ts` from the `cambridge/` environment. DW
   spawns the sidecar lazily on first CAM use on loopback, health-checks
   `/api/health`, and terminates it on app exit. Fully offline; no system
   Node required.
2. **Stateless bridge, authoritative native app.** DW owns the CAMJ document
   and sends it with each request. The bridge validates, computes, and
   returns JSON; it holds no session state. A sidecar crash loses nothing;
   DW policy modules stay the single owners of workflow state.
3. **Native rendering of engine output.** Toolpath previews return as move
   streams (rapid/cut segments with coordinates and metadata); DW's existing
   viewport renders them with the current toolpath layer. Parameter editing,
   dialogs, pickers: all ImGui.

## The Yank (Phase 1)

Deleted outright, together with their tests:

- `src/core/carve/` toolpath generation: `carve_job`,
  `direct_carve_workflow`, `direct_carve_tool_plan`, `toolpath_preview`,
  `direct_carve_probe_tool_diameter`, `direct_carve_operation_state`.
- `src/ui/panels/direct_carve_*` step panels and their adapters.
- Direct-Carve-specific wiring in `src/app/` (`application_carve_preparation`,
  carve adapters) — replaced by stubs.

Retained untouched:

- Project system, `ProjectSession`, Design Library, Project Plan.
- Tool database (`.vtdb`), materials system, machine profiles.
- Viewport, G-code parser/analyzer, `RunPackage`/`RunCoordinator`, GRBL
  controller, `dw_settings`.
- `PrepareCarveFlow` policy module (stage ordering/blockers are
  engine-agnostic); its evidence sources are re-bound in Phase 6.

During Phases 1–5 the Guided Carve stages render an explicit
"CAM rebuild in progress" stub. The build and full test suite stay green at
every session boundary; the *feature* is absent, the *suite* is not red.

## Bridge API growth (Phase 3)

Current bridge surface: `GET /api/health`, `GET /api/machines`,
`POST /api/job` (batch JobSpec → G-code). It grows to the full CAMJ v3
session surface:

| Endpoint (shape, not final naming) | Purpose |
|---|---|
| `POST /api/camj/validate` | Validate/migrate a CAMJ document, report compat warnings |
| `POST /api/feature/import` | STL/SVG → feature instances + placement defaults |
| `POST /api/operation/defaults` | Default parameter set for an operation kind + tool |
| `POST /api/toolpath` | Generate toolpaths for one operation or the whole document |
| `POST /api/preview` | Toolpath move streams for native viewport rendering |
| `GET  /api/postprocessors` | Available machine definitions / post options |
| `POST /api/export` | Post-processed G-code + stats + warnings |

Feature parity is the contract: every operation kind (pocket, edge route
inside/outside, v-carve, drilling, 2.5D, 3D surface) with its complete web-UI
parameter surface (tabs, leads, feed reduction, stepdown/stepover, cleanup
passes, etc.). Phase 3 begins by deriving a **parity checklist** from the
web UI's operation panels (`vendor/purecutcnc/src/components`) and checking
every item off; the checklist lives in `.planning/CAM-PARITY-CHECKLIST.md`.

## Interpreter layer (Phase 4)

`CamInterpreter` (C++, `src/core/cam/`) with a SQLite ID-mapping table
(schema migration):

- DW `.vtdb` tool records ↔ CAMJ tool definitions (geometry, cutting data).
- DW machine profiles ↔ engine machine definitions, including the bridge's
  FluidNC definition (not shipped upstream).
- Projection happens on demand when building a CAMJ document; reverse import
  creates DW records for unknown tools/machines found in an incoming CAMJ.
- Both libraries persist; the mapping table keeps identities stable in both
  directions. Vectric interchange is unaffected.

## CAM workspace and persistence (Phase 5)

A new "CAM" experience alongside Guided Workshop, Advanced Workbench, and
CNC Sender (stable layout ID, standard experience registration):

- Features tree (project models/sketches as engine features).
- Operations list + native per-kind parameter editors (ImGui forms).
- Tool and machine pickers reusing DW's existing browsers through the
  interpreter.
- Toolpath preview in the DW viewport; stats and warnings panel.
- Export panel (post-processor choice, G-code to project item).

Persistence: each CAM setup is a `.camj` file stored as a project item in the
project directory. Project Plan derives Carve-stage evidence from it.
Exported G-code becomes a normal project G-code item, which the existing Run
boundary consumes unchanged.

## Guided rebuild and Run handoff (Phase 6)

The Guided Prepare Carve stages (Design & Size, Material & Blank, Choose
Tool, Carve Preview) are rebuilt as a simplified front over the same CAMJ
document and bridge calls, driven by the retained `PrepareCarveFlow` policy.
Review & Run hands the exported G-code item to `RunPackage`/`RunCoordinator`
exactly as today.

## Phases

Each phase ends with the build green, the full suite green, `git diff
--check` clean, and the source-size policy passing (new files target 500
lines, 750 hard ceiling).

1. **Yank + stubs** — delete Direct Carve CAM and tests, stub Guided stages,
   v0.8.0 branch/version bookkeeping.
2. **Sidecar** — Bun-compiled `dw-cam-engine` binary, spawn/health/shutdown
   lifecycle, `CamEngineClient` (libcurl + nlohmann/json), installer payload
   updates (Linux `.run`, MSI, DMG) and packaging smoke.
3. **Bridge API** — full CAMJ session surface, parity checklist derivation,
   contract tests extending `smoke`/`verify-camj`.
4. **Interpreter + persistence** — tool/machine mapping + SQLite migration,
   `.camj` project items, Project Plan evidence binding.
5. **CAM workspace UI** — the parity build (largest phase; may split into
   sessions per panel group).
6. **Guided rebuild + Run handoff** — simplified stages on the engine.
7. **Validation** — E2E golden fixtures (CAMJ → G-code), packaging smoke
   with sidecar on all platforms, README/docs, release candidate.

## Error handling

- Sidecar fails to start / crashes: CAM surfaces show a clear native error
  state with a retry action; the rest of the app is unaffected. DW never
  blocks startup on the sidecar (lazy spawn).
- Bridge/engine errors return structured JSON errors; the UI presents them
  as actionable messages (validation errors point at the offending
  operation/parameter).
- Engine version drift: the client sends its expected contract version;
  mismatch produces an explicit "engine update required" state, never silent
  wrong output. Contract tests pin the submodule commit.

## Testing

- **C++ unit level:** `CamEngineClient` and `CamInterpreter` tested against
  a stub HTTP server (no Node in unit CI). Interpreter round-trip tests
  (tool → CAMJ → tool identity).
- **Contract level:** bridge tests run the real engine (`npm run smoke`,
  `verify-camj`, plus new endpoint tests) pinned to the submodule version.
- **E2E:** golden CAMJ fixtures → expected G-code hashes; River-Sign-style
  workflow test recreated over the new CAM path in Phase 6.
- Session gates and the source-size ratchet apply throughout.

## Risks

- **Parity surface is large.** Mitigated by the derived checklist and by
  splitting Phase 5 into per-panel sessions.
- **Bun compile of engine + wasm deps** (`manifold-3d`) needs early
  proof-of-concept — it is the first task of Phase 2; fallback is a Node SEA
  bundle, second fallback ships a minimal Node runtime alongside.
- **Guided regression window** (Phases 1–5): accepted cost of the big-bang
  approach; stubs keep the app honest about what is unavailable.
- **Upstream API drift** (as seen with `getBundledDefinition`): the
  interpreter and client touch only the bridge, and the bridge pins the
  engine; updates are deliberate submodule bumps followed by contract tests.
