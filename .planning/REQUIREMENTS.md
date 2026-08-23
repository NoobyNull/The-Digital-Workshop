# Requirements: Digital Workshop

**Defined:** 2026-03-09
**Last updated:** 2026-07-11 for v0.7.0 Project-Centered Workshop
**Core Value:** A woodworker can go from selecting a piece of wood and a cutting tool to safely running a CNC job with optimized feeds and speeds -- all without leaving the application.

## v0.7.0 Project-Centered Workshop Requirements

### Persistent Context (PCW)

- [x] **PCW-01**: One authoritative ProjectSession owns the active project, active project item, Library preview, route, return selection, and session generation.
- [x] **PCW-02**: The project name, dirty state, active item or preview state, current stage, and Back to Project action remain visible in the Guided shell.
- [x] **PCW-03**: Home is the single surface for New Project, Open Project, Recent Projects, and starting from a Library design.
- [x] **PCW-04**: Closing or switching projects clears or restores project-scoped focus predictably; stale asynchronous model loads cannot overwrite a newer session.
- [x] **PCW-05**: A valid last active project and active item can be resumed after application restart.

PCW-02 is complete: the shared shell keeps project identity, dirty state, active
item or Library preview, current area, machine state, and Back to Project
visible, while the Project Plan supplies the current six-stage preparation
state and one Continue action. The Guided-default switch remains a separate
Phase 48 human acceptance gate.

PCW-03 is complete: Home is the single panel surface for New, Open, Recent,
Library entry, and import entry, and its Library path starts a named project
from exactly one selected model. Conventional global menu commands remain
direct shortcuts; they do not create a second project-entry panel.

Session 08 completes the LIBX group: the render-independent policy now drives
visible purpose-specific UI, token-safe preview/return, atomic pinned mixed
membership, and purpose-preserving import completion.

### Design Library Relationship (LIBX)

- [x] **LIBX-01**: Design Library has explicit manage, start-project, and add-to-project purposes.
- [x] **LIBX-02**: Previewing a Library design is visibly labeled and cannot mutate project membership or project selection.
- [x] **LIBX-03**: Cancel or Back from preview restores the prior project selection in one visible action.
- [x] **LIBX-04**: Start Project and Add to Project are visible primary actions, not context-menu-only behavior.
- [x] **LIBX-05**: Adding Library assets is idempotent and uses the active project name in action copy.
- [x] **LIBX-06**: Imports completed from a picker return the imported assets to that picker purpose.

### Project Plan (PLANX)

- [x] **PLANX-01**: One hierarchical Project Plan replaces the duplicate Work Order and legacy Models/G-code/Materials/Costs/Cut Plans navigation sections.
- [x] **PLANX-02**: Every visible Project Plan row is actionable or explicitly informational.
- [x] **PLANX-03**: A deterministic builder derives the beginner stages, readiness, blockers, and one next action from project/open-item state.
- [x] **PLANX-04**: The beginner stages are Design & Size, Material & Blank, Choose Tool, Carve Preview, Machine Setup, and Review & Run.
- [x] **PLANX-05**: Material & Blank precedes Choose Tool so recommendations have visible material context.

### Prepare and Run Boundary (RUNX)

- [x] **RUNX-01**: Direct Carve setup captures and retains its initiating project, model, and operation identity.
- [x] **RUNX-02**: Direct Carve cannot silently create, activate, replace, or write output into another project.
- [x] **RUNX-03**: With no active project, Prepare Carve offers an explicit create-project action and performs no hidden project mutation.
- [x] **RUNX-04**: Prepare Carve produces an immutable RunPackage and cannot issue stream commands.
- [x] **RUNX-05**: Run coordination consumes RunPackage, owns the run lock, and cannot silently change setup identity.
- [x] **RUNX-06**: Active streaming rejects project/context switching while preserving emergency stop, pause, resume, and abort priority.
- [x] **RUNX-07**: When Run coordination is unavailable, preview and G-code export remain usable and no CNC command is emitted.

Phases 45 and 46 are complete: preparation owns editable exact-pinned state and
cannot express machine commands; RunCoordinator accepts only an immutable,
preflight-bound RunPackage, acquires the exact ProjectSession lock before real
stream submission, and owns progress plus every terminal cleanup path.

### Modularity and File Health (MODX)

- [x] **MODX-01**: ProjectSession is a render-independent module with typed transition results and direct tests.
- [x] **MODX-02**: LibraryPickerFlow is a render-independent module with explicit preview/add/cancel commands and direct tests.
- [x] **MODX-03**: ProjectPlanBuilder is a deterministic, persistence-free projection module with direct tests.
- [x] **MODX-04**: ProjectWorkshopController routes UI intents and service commands without owning workflow policy.
- [x] **MODX-05**: PrepareCarveFlow owns preparation state and RunCoordinator owns execution state; their only run handoff is immutable RunPackage.
- [x] **MODX-06**: New business behavior is not implemented through direct panel-to-panel dependencies.
- [x] **MODX-07**: Guided and Advanced experiences consume the same ProjectSession and repositories; disabling Guided emits no Guided-only commands.
- [x] **MODX-08**: Render-independent workshop modules build in a focused target that application and tests both link.
- [x] **MODX-09**: New or substantially rewritten files target 500 lines and may not exceed 750 lines.
- [x] **MODX-10**: `direct_carve_panel.cpp`, `viewport_panel.cpp`, `application_wiring.cpp`, `project_repository.cpp`, `config.cpp`, and touched Library files are decomposed along owned behavior boundaries defined in the master plan.

### Compatibility and Migration (MIGX)

- [x] **MIGX-01**: Guided is the default only after acceptance; Advanced Workbench remains available and uses the same project truth.
- [x] **MIGX-02**: Built-in layout migration is versioned and idempotent.
- [x] **MIGX-03**: Existing custom layout presets survive migration unchanged.
- [x] **MIGX-04**: Existing projects, Library records, imported G-code, and external `.nc` workflows remain usable.
- [x] **MIGX-05**: The UI does not claim global Library material assignment is project-specific; project setup material remains operation intent.

### Validation (VALX)

- [x] **VALX-01**: Automated tests cover project lifecycle, preview isolation, idempotent add, plan derivation, project pinning, run locks, migration, and close/reopen persistence.
- [x] **VALX-02**: Each execution session passes targeted tests, the full test binary, `git diff --check`, and an edited-file line-count audit.
- [x] **VALX-03**: Guided workflow is validated at 1366x768 and 4K with 100%, 150%, and 200% UI scale.
- [x] **VALX-04**: Critical status and navigation work by keyboard and do not depend on color alone.
- [ ] **VALX-05**: Five inexperienced hobbyists complete the canonical create/add/preview/return/prepare/close/reopen task without navigation coaching before Guided becomes default.

VALX-03 and VALX-04 are complete. The compile-gated real application produced
72 uncropped captures covering 12 deterministic River Sign states in all six
required environments. Independent manual review passed every cell, including
keyboard focus, text/icon state without color, compact-height safety controls,
and bounded 4K context/action layouts. The validated manifest is
`project-centered-workshop-release/ux-matrix/manifest.json` with SHA-256
`e5d0b4b3481aa4df9719e71cfb7913dce9860570ddb5a5e97d9615c872c3f98b`.
VALX-05 is prepared, not conducted. Consequently Advanced Workbench remains the
default and MIGX-01 is satisfied without claiming novice acceptance. The full
42/43 trace is in `project-centered-workshop-release/COMPLETION-AUDIT.md`.

## v0.6.0 Requirements

Requirements for Technical Debt Cleanup milestone. Verified via deep-dive code audit (2026-03-28).

### Rendering Bugs (RBUG)

- [ ] **RBUG-01**: Camera far plane restoration calls `setCamera()` so the renderer receives the updated projection matrix
- [ ] **RBUG-02**: Framebuffer `unbind()` restores the previous GL viewport -- no viewport leak to ImGui
- [ ] **RBUG-03**: `renderPoint()` restores `glPointSize(1.0f)` after drawing
- [ ] **RBUG-04**: `renderGCodeLines()` restores `glPointSize(1.0f)` after drawing the cutter dot

### Coordinate Space (COORD)

- [ ] **COORD-01**: An inline `gcodeToRenderer(Vec3)` helper centralizes the Y↔Z swap
- [ ] **COORD-02**: All 5 manual swap sites in viewport_panel.cpp use the helper -- no inline coordinate reordering remains

### GL State Safety (GLST)

- [x] **GLST-01**: A private `GLStateScope` RAII struct in Renderer handles `glEnable`/`glDisable` save/restore
- [x] **GLST-02**: All 5 renderer functions (renderMesh, renderToolpath, renderGrid, renderAxis, renderWireBox) use `GLStateScope` instead of manual toggle pairs

### CNC Controller (CNC)

- [x] **CNC-01**: A private `initializeConnection()` helper extracts the shared state init from `connect()` and `connectTcp()`

## Previous Milestone Requirements

<details>
<summary>v0.5.5 Unified 3D Viewport (15 requirements)</summary>

### Viewport Rendering (VPR)

### Viewport Rendering (VPR)

- [x] **VPR-01**: User can see 3D model mesh and G-code toolpath lines in the same viewport with a single shared camera
- [x] **VPR-02**: User can toggle model visibility on/off via a toolbar button
- [x] **VPR-03**: User can toggle toolpath visibility on/off via a toolbar button
- [x] **VPR-04**: User can toggle individual move types (rapids, cuts, plunges, retracts) on/off
- [x] **VPR-05**: User can toggle color-by-tool mode for multi-tool G-code
- [x] **VPR-06**: User can filter visible toolpath depth with a Z-clip slider
- [x] **VPR-07**: G-code cutting paths render with height-based color gradient showing depth

### Alignment (ALN)

- [x] **ALN-01**: Model transforms to machine space using FitParams so it overlays G-code correctly
- [x] **ALN-02**: System validates alignment by sampling ~1% of cutting points against mesh surface
- [x] **ALN-03**: External .nc files render in viewport standalone without requiring a model

### Simulation (SIM)

- [x] **SIM-01**: User can play/pause/scrub simulation playback in the viewport
- [x] **SIM-02**: Simulation shows completed path (green), current segment (yellow), and cutter position (red dot)

### Code Elimination (ELM)

- [ ] **ELM-01**: GCodePanel no longer owns Renderer, Camera, or Framebuffer
- [ ] **ELM-02**: GCodePanel retains text listing, statistics, CNC sender controls, and file management
- [ ] **ELM-03**: Mouse interaction is identical regardless of visible layers

</details>

## Previous Milestone Requirements

<details>
<summary>v0.5.0 Codebase Cleanup (14 requirements)</summary>

### Bug Fixes (BUG)
- [ ] **BUG-01**: GCodeMetadata in ImportQueue uses smart pointer instead of raw new/delete
- [ ] **BUG-02**: file::move() handles cross-filesystem moves via copy+delete fallback

### Monolithic Function Splits (SPLIT)
- [ ] **SPLIT-01**: Application::initWiring() decomposed into domain-specific wiring functions
- [ ] **SPLIT-02**: ImportQueue::processTask() decomposed into pipeline stage functions
- [ ] **SPLIT-03**: Schema::createTables() decomposed into per-table builder functions
- [ ] **SPLIT-04**: Config::load() decomposed into per-section parser functions
- [ ] **SPLIT-05**: Application::init() decomposed into initialization phase functions

### Duplicate Code Consolidation (DUP)
- [x] **DUP-01**: UI color constants centralized in single header
- [x] **DUP-02**: ImGui 2-column label/value table helper extracted
- [ ] **DUP-03**: Edit buffer management consolidated into utility function

### Code Quality (QUAL)
- [ ] **QUAL-01**: Hardcoded UI scale factors replaced with style-derived values
- [ ] **QUAL-02**: glClearColor uses theme/config values
- [ ] **QUAL-03**: application_wiring.cpp under 800 lines

### Oversized Files (SIZE)
- [ ] **SIZE-01**: Config::save() decomposed into per-section writer functions

</details>

## Future Requirements

### Deferred

- **ALN-04**: Auto-alignment for external G-code files without FitParams
- **VPR-08**: Orthographic projection toggle
- **VPR-09**: Multi-model overlay support
- **REF-01**: Config class split into concern-specific classes
- **REF-02**: Repository CRUD template base class
- **REF-03**: gcode_panel.cpp decomposition (partially addressed by ELM-01)
- **REF-06**: Windows serial port enumeration
- **REF-07**: Undo/redo system
- **REF-08**: 3MF deflate compression full integration

## Out of Scope

| Feature | Reason |
|---------|--------|
| Auto-alignment for external G-code | No FitParams available; would require surface matching solver |
| Ortho projection mode | Current perspective works; can add later |
| Multi-model overlay | One model + one toolpath is the use case |
| Toolpath editing in viewport | View-only; editing is in Direct Carve workflow |

## Traceability

### v0.7.0

| Requirement | Phase | Status |
|-------------|-------|--------|
| PCW-01 | Phase 41 | Complete |
| PCW-02 | Phase 42 | Complete |
| PCW-03 | Phase 42 | Complete |
| PCW-04 | Phase 41 | Complete |
| PCW-05 | Phase 42 | Complete |
| LIBX-01 | Phase 43 | Complete |
| LIBX-02 | Phase 43 | Complete |
| LIBX-03 | Phase 43 | Complete |
| LIBX-04 | Phase 43 | Complete |
| LIBX-05 | Phase 43 | Complete |
| LIBX-06 | Phase 43 | Complete |
| PLANX-01 | Phase 44 | Complete |
| PLANX-02 | Phase 44 | Complete |
| PLANX-03 | Phase 44 | Complete |
| PLANX-04 | Phase 44 | Complete |
| PLANX-05 | Phase 45 | Complete |
| RUNX-01 | Phase 45 | Complete |
| RUNX-02 | Phase 45 | Complete |
| RUNX-03 | Phase 45 | Complete |
| RUNX-04 | Phase 46 | Complete |
| RUNX-05 | Phase 46 | Complete |
| RUNX-06 | Phase 46 | Complete |
| RUNX-07 | Phase 46 | Complete |
| MODX-01 | Phase 41 | Complete |
| MODX-02 | Phase 43 | Complete |
| MODX-03 | Phase 44 | Complete |
| MODX-04 | Phase 42 | Complete |
| MODX-05 | Phases 45-46 | Complete |
| MODX-06 | Phases 41-47 | Complete |
| MODX-07 | Phase 47 | Complete |
| MODX-08 | Phase 40 | Complete |
| MODX-09 | Phases 40-48 | Complete |
| MODX-10 | Phases 43-47 | Complete |
| MIGX-01 | Phase 47 | Complete; default held |
| MIGX-02 | Phase 47 | Complete |
| MIGX-03 | Phase 47 | Complete |
| MIGX-04 | Phase 48 | Complete |
| MIGX-05 | Phases 43-45 | Complete |
| VALX-01 | Phase 48 | Complete |
| VALX-02 | Phases 40-48 | Complete |
| VALX-03 | Phase 48 | Complete; 72/72 visual cells pass |
| VALX-04 | Phase 48 | Complete; keyboard/color audit passes |
| VALX-05 | Phase 48 | Pending; protocol prepared |

**Coverage:**
- v0.7.0 requirements: 43 total
- Mapped to phases: 43
- Unmapped: 0

### v0.6.0

| Requirement | Phase | Status |
|-------------|-------|--------|
| RBUG-01 | Phase 36 | Pending |
| RBUG-02 | Phase 36 | Pending |
| RBUG-03 | Phase 36 | Pending |
| RBUG-04 | Phase 36 | Pending |
| COORD-01 | Phase 37 | Pending |
| COORD-02 | Phase 37 | Pending |
| GLST-01 | Phase 38 | Complete |
| GLST-02 | Phase 38 | Complete |
| CNC-01 | Phase 39 | Complete |

**Coverage:**
- v0.6.0 requirements: 9 total
- Mapped to phases: 9
- Unmapped: 0

### v0.5.5

| Requirement | Phase | Status |
|-------------|-------|--------|
| VPR-01 | Phase 31 | Complete |
| VPR-02 | Phase 32 | Complete |
| VPR-03 | Phase 32 | Complete |
| VPR-04 | Phase 32 | Complete |
| VPR-05 | Phase 32 | Complete |
| VPR-06 | Phase 32 | Complete |
| VPR-07 | Phase 31 | Complete |
| ALN-01 | Phase 33 | Complete |
| ALN-02 | Phase 33 | Complete |
| ALN-03 | Phase 31 | Complete |
| SIM-01 | Phase 34 | Complete |
| SIM-02 | Phase 34 | Complete |
| ELM-01 | Phase 35 | Pending |
| ELM-02 | Phase 35 | Pending |
| ELM-03 | Phase 35 | Pending |

---
*Requirements defined: 2026-03-09*
*Last updated: 2026-07-11 -- v0.7.0 traceability complete (42 engineering requirements complete; VALX-05 human gate pending)*
