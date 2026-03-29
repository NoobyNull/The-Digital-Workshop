# Requirements: Digital Workshop

**Defined:** 2026-03-09
**Core Value:** A woodworker can go from selecting a piece of wood and a cutting tool to safely running a CNC job with optimized feeds and speeds -- all without leaving the application.

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

- [ ] **GLST-01**: A private `GLStateScope` RAII struct in Renderer handles `glEnable`/`glDisable` save/restore
- [ ] **GLST-02**: All 5 renderer functions (renderMesh, renderToolpath, renderGrid, renderAxis, renderWireBox) use `GLStateScope` instead of manual toggle pairs

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

### v0.6.0

| Requirement | Phase | Status |
|-------------|-------|--------|
| RBUG-01 | Phase 36 | Pending |
| RBUG-02 | Phase 36 | Pending |
| RBUG-03 | Phase 36 | Pending |
| RBUG-04 | Phase 36 | Pending |
| COORD-01 | Phase 37 | Pending |
| COORD-02 | Phase 37 | Pending |
| GLST-01 | Phase 38 | Pending |
| GLST-02 | Phase 38 | Pending |
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
*Last updated: 2026-03-28 -- v0.6.0 traceability complete (9/9 requirements mapped)*
