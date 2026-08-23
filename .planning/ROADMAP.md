# Roadmap: Digital Workshop

## Milestones

- [x] **v0.1.x CNC Controller Suite** - Phases 1-8 (shipped 2026-02-25)
- [x] **v0.2.0 Sender Feature Parity** - Phases 9-13 (shipped 2026-02-27)
- [x] **v0.3.0 Direct Carve** - Phases 14-19 (completed 2026-02-28)
- [x] **v0.4.0 Shared Materials & Project Costing** - Phases 20-26 (completed 2026-03-05)
- [x] **v0.5.0 Codebase Cleanup & Simplification** - Phases 27-30 (completed 2026-04-18)
- [x] **v0.5.5 Unified 3D Viewport** - Phases 31-35 (completed 2026-03-09, P35 back-filled 2026-04-18)
- [x] **v0.6.0 Technical Debt Cleanup** - Phases 36-39 (completed 2026-03-29)
- [ ] **v0.7.0 Project-Centered Workshop** - Phases 40-48 (engineering and 72-state visual validation complete 2026-07-11; Guided-default human gate pending)

## Phases

<details>
<summary>v0.1.x CNC Controller Suite (Phases 1-8) - SHIPPED 2026-02-25</summary>

- [x] **Phase 1: Fix Foundation** - Resolve thread safety, error handling, and disconnect detection bugs in existing CncController (2026-02-24)
- [x] **Phase 2: Status Display** - Real-time DRO showing position, machine state, feed rate, and spindle RPM (2026-02-24)
- [x] **Phase 3: Manual Control** - Jog, homing, work zero, coordinate systems, and MDI console (2026-02-24)
- [x] **Phase 4: Tool Integration** - Connect tool database and material system to feeds/speeds calculator (2026-02-24)
- [x] **Phase 5: Job Streaming** - Progress tracking, time estimation, and feed rate deviation warnings (2026-02-24)
- [x] **Phase 6: Job Safety** - Pause, resume, e-stop, safe job resume, sensor display, and pre-flight checks (2026-02-24)
- [x] **Phase 7: Firmware Settings** - GRBL settings panel, backup/restore, machine tuning, and profile sync (2026-02-25)
- [x] **Phase 8: Macros** - Macro storage, execution, and built-in common operations (2026-02-25)

</details>

<details>
<summary>v0.2.0 Sender Feature Parity (Phases 9-13) - SHIPPED 2026-02-27</summary>

- [x] **Phase 9: Safety** - Configurable safety guards: long-press buttons, dead-man watchdog, door interlock, soft limits, pause-before-reset (2026-02-26)
- [x] **Phase 10: Core Sender** - Override controls, coolant toggles, alarm handling, status polling, precision jog, WCS quick-switch (2026-02-27)
- [x] **Phase 11: Niceties** - DRO click-to-zero, move-to dialog, diagonal jog, console tags, recent files, keyboard overrides, macro reorder, job alerts (2026-02-27)
- [x] **Phase 12: Extended** - Alarm/error references, probe workflows, M6 detection, nested macros, tool color viz, gamepad, TLS (2026-02-27)
- [x] **Phase 13: TCP/IP Transport** - Abstract byte stream interface, TCP socket, connection bar UI (2026-02-27)

</details>

<details>
<summary>v0.3.0 Direct Carve (Phases 14-19) - COMPLETED 2026-02-28</summary>

- [x] **Phase 14: Heightmap Engine** - Rasterize STL to 2D heightmap grid with configurable resolution, model fitting, background computation (2026-02-28)
- [x] **Phase 15: Model Analysis** - Minimum feature radius detection, island identification, visual annotation on heightmap preview (2026-02-28)
- [x] **Phase 16: Tool Recommendation** - Automatic tool selection from database based on model analysis (2026-02-28)
- [x] **Phase 17: Toolpath Generation** - Raster scan toolpath with configurable axis, stepover, milling direction, surgical island clearing (2026-02-28)
- [x] **Phase 18: Guided Workflow** - Step-by-step wizard with safety checks, preview, outline test (2026-02-28)
- [x] **Phase 19: Streaming Integration** - Point-by-point streaming via CncController, pause/resume/abort, G-code export (2026-02-28)

</details>

<details>
<summary>v0.4.0 Shared Materials & Project Costing (Phases 20-26) - COMPLETED 2026-03-05</summary>

- [x] **Phase 20: Data Foundation & Rename** - Stock sizes DB table, project cost JSON schema, rename estimator to costing (2026-03-04)
- [x] **Phase 21: Material Stock Sizes** - Parent-child material/stock model with dimensions, pricing, and common size suggestions (2026-03-05)
- [x] **Phase 22: Consumables & Live Pricing** - Consumable items, tool cost entries, live DB pricing for non-material costs (2026-03-05)
- [x] **Phase 23: CLO Material Integration** - Cut optimizer uses shared material/stock system with waste breakdown and scrap tracking (2026-03-05)
- [x] **Phase 24: Project Costing Engine** - Cost entry types (material, labor, overhead), estimated vs actual columns, persistence (2026-03-05)
- [x] **Phase 25: CLO-to-Costing Pipeline** - CLO results auto-push into project costing, project-level cost aggregation (2026-03-05)
- [x] **Phase 26: Estimate & Order Views** - Sale price and margin, estimate vs order toggle, formatted print output (2026-03-05)

</details>

<details>
<summary>v0.5.0 Codebase Cleanup & Simplification (Phases 27-30) - IN PROGRESS</summary>

- [x] **Phase 27: Bug Fixes & Safety** - Fix raw new/delete in ImportQueue and cross-filesystem file::move() fallback (executed 2026-03-04 in commit 6751b47; tracker back-filled 2026-04-18)
- [x] **Phase 28: Monolithic Function Splits** - Decompose five oversized functions and bring all split-out files under the 800-line limit (executed 2026-03; tracker back-filled 2026-04-18)
- [x] **Phase 29: Duplicate Code Consolidation** - Centralize UI color constants, ImGui table helpers, and edit buffer management (completed 2026-03-05; tracker back-filled 2026-04-18)
- [x] **Phase 30: Code Quality Polish** - Replace hardcoded UI scale factors and glClearColor constants with style/config-derived values (completed 2026-04-18)

</details>

### v0.5.5 Unified 3D Viewport (Phases 31-35)

**Milestone Goal:** Merge G-code preview rendering into the main ViewportPanel so all 3D content (model mesh, toolpath lines) shares one camera, one renderer, and one set of controls -- eliminating duplicated rendering code and unifying user interaction.

- [x] **Phase 31: Core Toolpath Rendering** - G-code lines render in ViewportPanel with height-based depth coloring and shared camera (completed 2026-03-09)
- [x] **Phase 32: Viewport Toolbar & Toggles** - Toggle buttons and sliders for model, toolpath, move types, color-by-tool, and Z-clip (completed 2026-03-09)
- [x] **Phase 33: Model-Toolpath Alignment** - FitParams transform overlays model and G-code correctly with point-match validation (completed 2026-03-09)
- [x] **Phase 34: Simulation Playback** - Play/pause/scrub simulation with completed/current/cutter visualization in viewport (completed 2026-03-09)
- [x] **Phase 35: GCodePanel Rendering Elimination** - Strip rendering infrastructure from GCodePanel, unify mouse interaction across layers (executed 2026-03; tracker back-filled 2026-04-18)

### v0.6.0 Technical Debt Cleanup (Phases 36-39)

**Milestone Goal:** Fix four verified rendering bugs, centralize scattered coordinate space transformations into a single helper, introduce RAII-based GL state management to eliminate manual enable/disable pairs, and deduplicate CNC connection initialization -- all changes identified by deep-dive code audit.

- [x] **Phase 36: Critical Rendering Bugs** - Fix camera far plane propagation, framebuffer viewport leak, and two glPointSize leaks (completed 2026-03-29)
- [x] **Phase 37: Coordinate Space Consolidation** - Introduce gcodeToRenderer() helper and replace all 5 manual Y↔Z swap sites (completed 2026-03-29)
- [x] **Phase 38: GL State Safety** - Introduce GLStateScope RAII struct and replace manual toggle pairs in 5 renderer functions (completed 2026-03-29)
- [x] **Phase 39: CNC Controller Dedup** - Extract shared connection initialization into a private initializeConnection() helper (completed 2026-03-29)

### v0.7.0 Project-Centered Workshop (Phases 40-48)

**Milestone Goal:** Make Project the persistent preparation context for inexperienced hobbyists, make Design Library an explicit reusable catalog/picker, and hand an immutable verified setup into protected Run CNC execution. Build the experience through independently testable modules while shrinking every touched monolithic file.

- **Master plan:** `.planning/PROJECT-CENTERED-WORKSHOP-PLAN.md`
- **Execution sessions:** `.planning/PROJECT-CENTERED-WORKSHOP-SESSIONS.md`

- [x] **Phase 40: Baseline and Module Guardrails** - Preserve current dirty work, record the canonical journey, create the render-independent workshop target, and establish file-size/module-contract checks. (2026-07-10)
- [x] **Phase 41: Authoritative Project Session** - Centralize project activation, selection origin, preview/return state, route transitions, generation tokens, and streaming conflict priority. (2026-07-10)
- [x] **Phase 42: Home and Persistent Project Shell** - One authoritative Home, named project lifecycle, storage-validated restart resume, and deterministic close/reset behavior are complete. (2026-07-10)
- [x] **Phase 43: Contextual Design Library** - Explicit manage/start/add UI, token-safe preview and return, atomic mixed membership, purpose-preserving imports, linked-delete protection, and Library extraction are complete. (2026-07-10)
- [x] **Phase 44: Project Plan Navigation** - One deterministic hierarchy now drives the Project panel, with one Continue action, six textual stages, generic exact-identity activation, and secondary project details. (2026-07-10)
- [x] **Phase 45: Modular Prepare Carve** - Exact pinning, explicit project handling, headless preparation policy, stale-result guards, Material-before-Tool order, and four snapshot-guided focused preparation views are complete. (2026-07-10)
- [x] **Phase 46: Protected Run Boundary** - Immutable RunPackage, RunCoordinator, exact run locks, real stream submission, Machine Setup, Review & Run, history, and terminal cleanup are complete. (2026-07-10)
- [x] **Phase 47: Layout, Viewport, and Advanced Compatibility** - Versioned/idempotent built-in migration, custom-preset preservation, shared Guided/Advanced truth, and viewport decomposition are complete. (2026-07-10)
- [ ] **Phase 48: Integrated Validation and Release Readiness** - Automated E2E, accessibility heuristics, 72/72 independently reviewed workflow captures, packages, source audit, and handoff pass; only the five-person novice study remains before Guided becomes default.

**Execution order:** 40 -> 41 -> 42 -> 43 -> 44 -> 45 -> 46 -> 47 -> 48. Do not parallelize sessions that share ProjectSession, Direct Carve state, or layout migration. Limited isolated worktree parallelism is allowed only after the owning contract is frozen.

## Phase Details (v0.5.0)

### Phase 27: Bug Fixes & Safety
**Goal**: Known correctness bugs are fixed -- ImportQueue manages GCodeMetadata with a smart pointer and file::move() handles cross-filesystem moves without data loss
**Depends on**: Phase 26 (v0.4.0 complete)
**Requirements**: BUG-01, BUG-02
**Success Criteria** (what must be TRUE):
  1. ImportQueue creates GCodeMetadata via std::unique_ptr or std::make_unique -- no raw new or delete calls exist in the import pipeline
  2. Moving a file between different filesystems (e.g., /tmp to /data) succeeds by falling back to copy+delete, with the original removed only after a verified copy
  3. All 931+ tests continue to pass after both fixes
**Plans**: TBD

Plans:
- [ ] 27-01: Replace raw new/delete for GCodeMetadata with smart pointer in ImportQueue
- [ ] 27-02: Add cross-filesystem copy+delete fallback to file::move()

### Phase 28: Monolithic Function Splits
**Goal**: The five largest functions in the codebase are each decomposed into focused, named sub-functions -- no single function exceeds a readable length and all split-out files stay under 800 lines
**Depends on**: Phase 27
**Requirements**: SPLIT-01, SPLIT-02, SPLIT-03, SPLIT-04, SPLIT-05, SIZE-01, QUAL-03
**Success Criteria** (what must be TRUE):
  1. Application::initWiring() delegates to domain-specific wiring functions (e.g., wireImport, wireMaterials, wireCosting) -- the top-level function is a sequenced call list, not a 900-line body
  2. ImportQueue::processTask() is decomposed into named pipeline stage functions -- each stage is independently readable and the dispatch loop is short
  3. Schema::createTables() calls per-table builder functions -- adding a new table means adding one function, not appending to a monolith
  4. Config::load() and Config::save() each delegate to per-section parser/writer functions with symmetrical structure
  5. Application::init() is decomposed into initialization phase functions and application_wiring.cpp is under 800 lines
  6. All 931+ tests continue to pass after all splits
**Plans**: TBD

Plans:
- [ ] 28-01: Decompose Application::initWiring() into domain wiring functions
- [ ] 28-02: Decompose ImportQueue::processTask() into pipeline stage functions
- [ ] 28-03: Decompose Schema::createTables() into per-table builder functions
- [ ] 28-04: Decompose Config::load() and Config::save() into per-section functions
- [ ] 28-05: Decompose Application::init() and verify application_wiring.cpp line count

### Phase 29: Duplicate Code Consolidation
**Goal**: Repeated UI patterns that appear across multiple files are extracted into single authoritative locations -- color constants, table layout helpers, and edit buffer utilities each live in one place
**Depends on**: Phase 28
**Requirements**: DUP-01, DUP-02, DUP-03
**Success Criteria** (what must be TRUE):
  1. A single header defines the canonical error-red, success-green, and warning-yellow ImVec4 constants -- no panel file defines its own copies of these colors
  2. A reusable helper function renders the ImGui 2-column label/value table pattern -- call sites pass label and value, not repeated BeginTable/TableSetColumnIndex boilerplate
  3. Inline edit buffer management (memset+snprintf pairs) is replaced by a utility function or wrapper at all call sites -- no panel contains raw memset+snprintf for buffer prep
  4. All 931+ tests continue to pass after consolidation
**Plans**: TBD

Plans:
- [x] 29-01: Centralize UI color constants (error, success, warning) in a shared header
- [ ] 29-02: Extract ImGui 2-column label/value table into a reusable helper
- [ ] 29-03: Consolidate inline edit buffer management into a utility function

### Phase 30: Code Quality Polish
**Goal**: All hardcoded numeric values in visual and GUI code are replaced with values derived from the style system or config -- the codebase satisfies the no-hardcoded-values policy throughout
**Depends on**: Phase 29
**Requirements**: QUAL-01, QUAL-02
**Success Criteria** (what must be TRUE):
  1. Panel code contains no hardcoded UI scale factors -- spacing, padding, and size multipliers reference ImGui style members or config values, not raw numeric literals
  2. glClearColor is called with values from the active theme or config, not hardcoded float constants -- changing the theme background updates the clear color
  3. All 931+ tests continue to pass after the replacements
**Plans**: TBD

Plans:
- [ ] 30-01: Replace hardcoded UI scale factors in panel code with style-derived values
- [ ] 30-02: Replace hardcoded glClearColor constants with theme/config values

## Phase Details (v0.5.5)

### Phase 31: Core Toolpath Rendering
**Goal**: G-code toolpath lines render directly in ViewportPanel using the existing shared camera, with height-based depth coloring, so the user sees both the 3D model and toolpath in one viewport
**Depends on**: Phase 30 (v0.5.0 complete)
**Requirements**: VPR-01, VPR-07, ALN-03
**Success Criteria** (what must be TRUE):
  1. User sees 3D model mesh and G-code toolpath lines together in the main viewport -- one camera controls orbit, pan, and zoom for both
  2. G-code cutting paths display a height-based color gradient showing carving depth (the height-line shader from GCodePanel works in ViewportPanel)
  3. Opening an external .nc file without a loaded model renders the toolpath in the viewport on its own -- no model is required
  4. Toolpath line geometry uploads to GPU via the existing m_gpuToolpath infrastructure in ViewportPanel
**Plans**: 2 plans

Plans:
- [x] 31-01-PLAN.md — Height-line shader and G-code line rendering pipeline
- [x] 31-02-PLAN.md — GCode-to-Viewport callback wiring

### Phase 32: Viewport Toolbar & Toggles
**Goal**: User has fine-grained control over what is visible in the unified viewport through toolbar buttons and sliders
**Depends on**: Phase 31
**Requirements**: VPR-02, VPR-03, VPR-04, VPR-05, VPR-06
**Success Criteria** (what must be TRUE):
  1. User can toggle model mesh visibility on/off via a toolbar button -- the mesh disappears/reappears without affecting toolpath display
  2. User can toggle toolpath visibility on/off via a toolbar button -- the toolpath disappears/reappears without affecting model display
  3. User can independently toggle rapids, cuts, plunges, and retracts on/off -- each move type appears or disappears without affecting other types
  4. User can toggle color-by-tool mode that colors toolpath segments by their tool number for multi-tool G-code files
  5. User can drag a Z-clip slider that hides toolpath geometry above or below a chosen depth -- only the selected depth range is visible
**Plans**: 1 plan

Plans:
- [x] 32-01-PLAN.md — Filter state, toolbar UI, conditional rendering, color-by-tool, Z-clip

### Phase 33: Model-Toolpath Alignment
**Goal**: Model and toolpath overlay correctly in the viewport using FitParams, with point-match validation confirming the alignment is accurate
**Depends on**: Phase 32
**Requirements**: ALN-01, ALN-02
**Success Criteria** (what must be TRUE):
  1. When a Direct Carve workflow has computed FitParams (scale, offsetX, offsetY, depthMm), the model mesh transforms to machine space so it visually overlays the G-code toolpath at the correct position and scale
  2. System samples approximately 1% of cutting points from the toolpath and validates they lie on or near the mesh surface -- a pass/fail indicator shows whether model and toolpath correspond
  3. Alignment updates live when FitParams change (e.g., user adjusts stock dimensions in Direct Carve) -- the model repositions without requiring a manual refresh
**Plans**: 2 plans

Plans:
- [x] 33-01-PLAN.md — FitParams model matrix computation, DirectCarvePanel callback, wiring
- [x] 33-02-PLAN.md — AlignmentValidator utility and toolbar pass/fail indicator

### Phase 34: Simulation Playback
**Goal**: User can play back the cutting simulation in the unified viewport with transport controls and visual progress feedback
**Depends on**: Phase 31
**Requirements**: SIM-01, SIM-02
**Success Criteria** (what must be TRUE):
  1. User can play, pause, and scrub (seek via slider) through the G-code simulation in the viewport -- playback advances through toolpath segments at a controllable speed
  2. Completed path segments render in green, the currently executing segment renders in yellow, and a red dot marks the cutter position -- all three are visually distinct during playback
  3. Simulation controls are accessible from the viewport toolbar area -- the user does not need to switch to GCodePanel to start or control simulation
**Plans**: 1 plan

Plans:
- [x] 34-01-PLAN.md — Simulation state, overlay rendering, toolbar controls, and wiring

### Phase 35: GCodePanel Rendering Elimination
**Goal**: GCodePanel no longer contains any 3D rendering infrastructure -- all viewport rendering is owned by ViewportPanel, and mouse interaction is unified across all visible layers
**Depends on**: Phase 31, Phase 32, Phase 33, Phase 34
**Requirements**: ELM-01, ELM-02, ELM-03
**Success Criteria** (what must be TRUE):
  1. GCodePanel does not own or instantiate a Renderer, Camera, or Framebuffer -- all 3D rendering code has been removed from gcode_panel.cpp/h
  2. GCodePanel still provides its text-based G-code listing, file statistics, CNC sender controls, and file open/save functionality -- no non-rendering features are lost
  3. Mouse interaction (orbit, pan, zoom) in the viewport behaves identically whether the user is viewing model-only, toolpath-only, or both layers together -- no mode-dependent input differences
  4. The overall line count of gcode_panel.cpp decreases significantly from its pre-milestone size due to removed rendering code
**Plans**: 1 plan

Plans:
- [ ] 35-01-PLAN.md — Strip rendering infrastructure from GCodePanel, add G-code text listing

## Phase Details (v0.6.0)

### Phase 36: Critical Rendering Bugs
**Goal**: Four verified rendering bugs are eliminated -- the renderer always holds the current projection matrix, framebuffer unbind does not leak the viewport rect, and glPointSize is always restored after use
**Depends on**: Phase 35 (v0.5.5 complete)
**Requirements**: RBUG-01, RBUG-02, RBUG-03, RBUG-04
**Success Criteria** (what must be TRUE):
  1. After restoring the camera far plane in viewport_panel.cpp, calling setCamera() propagates the updated projection matrix to the renderer -- the scene does not briefly render with a stale projection
  2. After Framebuffer::unbind() returns, the GL viewport rect matches what it was before bind() was called -- ImGui widgets that render after an unbind are not clipped or scaled incorrectly
  3. After renderPoint() returns, glPointSize is 1.0f -- subsequent draw calls that do not set a point size are not affected by the cutter dot's size
  4. After renderGCodeLines() returns, glPointSize is 1.0f -- the cutter position dot does not affect point sizes elsewhere in the frame
**Plans**: 2 plans

Plans:
- [x] 36-01-PLAN.md — Framebuffer viewport save/restore in bind/unbind
- [x] 36-02-PLAN.md — Camera projection propagation and glPointSize restore fixes

### Phase 37: Coordinate Space Consolidation
**Goal**: A single inline helper function owns the Y↔Z coordinate swap for G-code-to-renderer conversion, and all five manual swap sites in viewport_panel.cpp use it -- no inline coordinate reordering remains scattered through the file
**Depends on**: Phase 36
**Requirements**: COORD-01, COORD-02
**Success Criteria** (what must be TRUE):
  1. An inline gcodeToRenderer(Vec3) function exists that takes a G-code space coordinate and returns the renderer space coordinate with Y and Z swapped
  2. A search for manual Y↔Z swap patterns (assigning .y to .z or .z to .y across the five known sites at viewport_panel.cpp lines 632, 694-698, 1057-1058, 1543-1548, and 1562-1566) finds zero instances -- all sites call gcodeToRenderer() instead
  3. Toolpath, simulation, and alignment visuals render identically before and after the consolidation -- no coordinate is incorrectly transformed
**Plans**: 1 plan

Plans:
- [x] 37-01-PLAN.md — Create gcodeToRenderer helper and replace all 5 manual Y<->Z swap sites (completed 2026-03-29)

### Phase 38: GL State Safety
**Goal**: A private GLStateScope RAII struct in Renderer saves and restores a named GL capability on construction/destruction, replacing manual enable/disable pairs in five renderer functions -- no render function leaves GL state modified after it returns
**Depends on**: Phase 36
**Requirements**: GLST-01, GLST-02
**Success Criteria** (what must be TRUE):
  1. A GLStateScope struct exists in renderer.cpp/h that takes a GLenum capability, saves its current state with glIsEnabled, enables or disables it as requested, and restores the original state on destruction
  2. renderMesh, renderToolpath, renderGrid, renderAxis, and renderWireBox each use GLStateScope in place of their manual glEnable/glDisable toggle pairs -- no manual before/after toggle pattern remains in these functions
  3. Rendering output is visually identical before and after the refactor -- no model, toolpath, grid, axis, or wire-box appearance changes
  4. All 931+ tests continue to pass after the refactor
**Plans**: 1 plan

Plans:
- [x] 38-01-PLAN.md — GLStateScope struct and 5-function toggle replacement

### Phase 39: CNC Controller Dedup
**Goal**: The shared state initialization code executed at the start of both connect() and connectTcp() is extracted into a single private helper -- neither method duplicates that logic
**Depends on**: Phase 36
**Requirements**: CNC-01
**Success Criteria** (what must be TRUE):
  1. A private initializeConnection() method exists in CncController that contains the 8 lines of shared state init (reset state flags, clear buffers, initialize status fields) that were previously duplicated between connect() and connectTcp()
  2. connect() and connectTcp() each call initializeConnection() at the point where the duplicated block previously appeared -- neither method contains an inline copy of that initialization block
  3. CNC connection behavior (serial and TCP) is identical before and after the extraction -- existing connection tests pass without modification
**Plans**: 1 plan

Plans:
- [x] 39-01-PLAN.md — Extract initializeConnection() helper and update connect()/connectTcp()

## Progress (v0.5.0)

**Execution Order:** Phase 27 -> 28 -> 29 -> 30 (sequential -- each builds on the previous)

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 27. Bug Fixes & Safety | v0.5.0 | 0/2 | Not started | - |
| 28. Monolithic Function Splits | v0.5.0 | 0/5 | Not started | - |
| 29. Duplicate Code Consolidation | v0.5.0 | 1/3 | In progress | - |
| 30. Code Quality Polish | v0.5.0 | 0/2 | Not started | - |

## Progress (v0.5.5)

**Execution Order:** Phase 31 -> 32 -> 33 -> 34 -> 35 (Phase 34 depends on 31 only; Phase 35 depends on all prior phases)

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 31. Core Toolpath Rendering | 2/2 | Complete   | 2026-03-09 | - |
| 32. Viewport Toolbar & Toggles | 1/1 | Complete   | 2026-03-09 | - |
| 33. Model-Toolpath Alignment | 2/2 | Complete   | 2026-03-09 | - |
| 34. Simulation Playback | 1/1 | Complete   | 2026-03-09 | - |
| 35. GCodePanel Rendering Elimination | v0.5.5 | 0/1 | Not started | - |

## Progress (v0.6.0)

**Execution Order:** Phase 36 first (bugs must be fixed before refactors build on them); Phase 37 and Phase 38 depend on Phase 36 and can execute in either order; Phase 39 is independent and can execute in parallel with 37 and 38.

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 36. Critical Rendering Bugs | v0.6.0 | 2/2 | Complete    | 2026-03-29 |
| 37. Coordinate Space Consolidation | v0.6.0 | 0/1 | Not started | - |
| 38. GL State Safety | v0.6.0 | 1/1 | Complete   | 2026-03-29 |
| 39. CNC Controller Dedup | v0.6.0 | 1/1 | Complete   | 2026-03-29 |

## Progress (v0.7.0)

| Phase | Planned sessions | Status | Completed |
|-------|-----------------:|--------|-----------|
| 40. Baseline and Module Guardrails | 2 | Complete | 2026-07-10 |
| 41. Authoritative Project Session | 2 | Complete | 2026-07-10 |
| 42. Home and Persistent Project Shell | 2 | Complete | 2026-07-10 |
| 43. Contextual Design Library | 2 | Complete | 2026-07-10 |
| 44. Project Plan Navigation | 2 | Complete | 2026-07-10 |
| 45. Modular Prepare Carve | 3 | Complete | 2026-07-10 |
| 46. Protected Run Boundary | 2 | Complete | 2026-07-10 |
| 47. Layout, Viewport, and Advanced Compatibility | 2 | Complete | 2026-07-10 |
| 48. Integrated Validation and Release Readiness | 1 | Engineering + visual pass; human gate pending | - |

**Total:** 18 planned execution sessions. Each session ends with direct tests, the full test binary, `git diff --check`, edited-file line counts, and a written handoff.

---
*Roadmap created: 2026-02-24*
*Last updated: 2026-07-11 -- Sessions 01-17 complete; Session 18 engineering and 72-state visual pass, Guided-default human gate pending*
