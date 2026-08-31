---
milestone: v0.7.0
name: Project-Centered Workshop Sessions
status: release-gate-pending
created: 2026-07-10
planned_sessions: 18
completed_sessions: 17
engineering_sessions_executed: 18
master_plan: .planning/PROJECT-CENTERED-WORKSHOP-PLAN.md
---

# Project-Centered Workshop — Execution Sessions

This companion contains the session-by-session execution sequence. Module contracts, scope, architectural rules, migration policy, and Definition of Done remain in the master plan.

## Phase and Session Plan

### Phase 40 — Baseline and Module Guardrails

**Goal:** establish a safe execution baseline and independently testable workshop module boundary.

#### Session 01 — Preserve and measure

**Status:** Complete — 2026-07-10. Evidence is recorded in
`PROJECT-CENTERED-WORKSHOP-BASELINE.md`.

- inventory and preserve the three existing dirty themes;
- run the full current test suite and record count;
- capture Home, Library, Project, Direct Carve, and Run baseline screens;
- record current file sizes and navigation behavior;
- create the canonical `River Sign` usability fixture and task script.

**Gate:** baseline evidence exists; no current work is lost; source behavior is unchanged.

#### Session 02 — Workshop core target and contract skeletons

**Status:** Complete — 2026-07-10. `dw_workshop_core`, its isolated test target,
and the repository/session source-size ratchet are green.

- add `dw_workshop_core` target;
- add empty/minimal typed contracts for project context, transitions, and commands;
- add module-boundary tests and dependency rules;
- add a repeatable edited-file line-count check.

**Gate:** app and tests link the new target; disabling Guided rendering produces no Guided-only actions.

### Phase 41 — Authoritative Project Session

**Goal:** one owner controls project identity, project selection, previews, and route transitions.

#### Session 03 — ProjectSession state machine

**Status:** Complete — 2026-07-10. Pure transitions, tokenized confirmation,
preview restoration, explicit run identity, conflict priority, and generation
rules pass in an isolated 25-test target.

- implement context snapshot, transition results, route, origin, return selection, and generation token;
- unit-test all transition and conflict-priority rules;
- do not change visible UI.

**Gate:** deterministic state tests pass without ImGui, database, or filesystem access.

#### Session 04 — Project lifecycle integration

**Status:** Complete — 2026-07-10. All project entry points now use one
token-aware activation/close gateway. Folder import is transactional and
portable, temporary ownership survives restart, stale async loads are rejected,
and the final gate passes 1,226 tests across 136 suites.

- replace direct active-project pointer assignment with one activation/close path;
- route New, Open, Recent, Import, Close, and project switching through it;
- reset/restore Workspace focus predictably;
- reject stale asynchronous model loads after session changes.

**Gate:** no unowned `setCurrentProject()` call remains; existing project persistence tests and new lifecycle tests pass.

### Phase 42 — Home and Persistent Project Shell

**Goal:** establish one starting surface and an always-visible active-project identity.

#### Session 05 — Project context bar

**Status:** Complete — 2026-07-10. The shared shell now renders a
snapshot-driven project context bar, and workshop composition is extracted from
the main application/UI wiring files. The full gate passes 1,239 tests across
137 suites.

- add project name, dirty marker, active item/preview state, machine status, and Back to Project;
- keep the Guided surface behind the experience seam;
- extract workshop wiring from `application_wiring.cpp`.

**Gate:** context bar is driven only by ProjectSession snapshots and works in Guided and Advanced modes.

The live application remains in Advanced mode until Phase 47, but the bar is
shared by both experiences and the controller's enabled/disabled paths are
covered directly. `Back to Project` intentionally dispatches an explicit
Project navigation command; Library cancel/restore remains a separate Phase 43
picker behavior. The bar's `AREA` value is the current surface, not the future
six-step preparation stage, so PCW-02 remains partial until Phase 44.

#### Session 06 — Home consolidation and resume

**Status:** Complete — 2026-07-10. Home is now the only New/Open/Recent
surface, named projects publish through an owned lifecycle transaction, and a
versioned sidecar restores only storage-validated project/item identities. The
full gate passes 1,297 tests across 143 suites plus all 1,371 CTest cases.

- merge Start Page and empty Project actions into Home;
- add named project creation before database insertion;
- resume the last active project and active item when valid;
- make Close Project clear project-scoped focus and return Home.

**Gate:** one New/Open/Recent surface remains; close/reopen restores the canonical journey correctly.

Home uses the authoritative Home route and a full-width layout rather than
leaving empty Library, Project, Properties, or Viewport panels visible. Project
creation prepares temporary owned storage, activates it through ProjectSession,
then publishes it; failed publication remains retryable and cannot discard an
unrelated project. Resume state is separate from general preferences, written
atomically, and repaired or cleared through typed outcomes. Async model and
Direct Carve restoration stays pending until the exact project item owns the
result, while explicit Close clears the bookmark and all project-owned focus.
Visual checks cover clean Home at 1600x900 and 1000x700, named creation,
close-to-Home, and restart into the saved project.

### Phase 43 — Contextual Design Library

**Goal:** Library becomes an explicit catalog/picker and cannot silently steal project context.

#### Session 07 — LibraryPickerFlow

**Status:** Complete — 2026-07-10. A separate `dw_design_library` target now
owns the render- and persistence-independent picker policy. Twenty-six direct
tests cover purpose, ownership, idempotence, completion races, and exact
return/restore behavior; the full gate passes 1,323 tests across 144 suites and
all 1,423 sequential CTest cases.

- implement manage, start-project, and add-to-project purposes;
- implement preview/cancel/restore and idempotent add commands;
- remove direct ProjectManager mutation from new picker behavior.

**Gate:** pure flow tests prove preview and membership are independent.

The flow has explicit Manage, Start Project, and Add to Project purposes rather
than inferring behavior from whichever panels happen to be open. Temporary
selection, successful preview, and durable membership are separate state. Add
requests contain only unique missing assets and remain pinned to the captured
ProjectId; Start accepts exactly one model and can complete only into the newly
activated project. Preview, Add, Start, and Restore use monotonic ownership
tokens. Restore also carries the exact ProjectSession generation and remains
pending until the destination route/project/item is acknowledged, so delayed
callbacks and look-alike later state cannot overwrite a newer journey. Imports
join the current temporary selection without implicitly previewing or adding.

#### Session 08 — Library UI and import handoff

**Status:** Complete — 2026-07-10. The visible purpose-specific picker, atomic
mixed membership, import handoff, acknowledged return, guarded deletion, and
Library/app extraction pass the complete Session 08 gate.

- add visible `Preview`, `Start project with this design`, and `Add to <project>` actions;
- label Library preview visibly;
- return imported assets to the active picker purpose;
- unify Model and G-code membership behind one pinned, typed persistence service;
- block deletion of a source linked to any project instead of cascading behind live project state;
- split Library item presentation/action policy into submodules.

**Gate:** canonical add/preview/return integration tests pass; mixed membership is
all-or-nothing and linked deletion is non-destructive; Library files meet size targets.

Evidence: 1,357 full-binary tests across 147 suites, 35 isolated picker tests,
and all 1,471 sequential CTest cases pass. Diagnostic startup succeeds on the
live OpenGL/database path. The
source-size policy passes across 440 production files; all new or substantially
edited Session 08 units are at or below 499 lines, and
`build/source-size/session-09-start.tsv` records the next boundary.

### Phase 44 — Project Plan as Navigation Authority

**Goal:** one project hierarchy and one next action replace competing project representations.

#### Session 09 — ProjectPlanBuilder and repository seam

**Status:** Complete — 2026-07-10. `dw_project_plan` is a persistence-free,
UI-free projection target. It builds a canonical parent/child hierarchy,
preserves malformed items as diagnostic roots, emits the six fixed beginner
stages in material-before-tool order, keeps unknown safety evidence distinct
from ready evidence, and returns exactly one typed next-action result. Active
machine runs retain safety priority; stale/missing required items block at the
earliest affected stage; external G-code remains a supported branch.

- build true parent/child projection from open items;
- derive six beginner stages, readiness, blockers, and next action;
- separate project open-item implementation from oversized project repository code.

**Gate:** projection tests cover empty, partial, stale, ready, running, and complete projects; repository files meet limits.

Evidence: all 17 isolated Project Plan projection/boundary tests, 1,374
full-binary tests across 149 suites, and all 1,505 sequential CTest cases pass.
The source-size policy passes across 449 production files with ten existing
target warnings. The Project Plan production units are 434, 211, 176, and 61
lines. `project_repository.cpp` fell from 1,371 to 270 lines; open-item CRUD,
synchronization, validation, mapping, and shared detail units are independently
owned and every repository unit is at or below 477 lines.
`build/source-size/session-10-start.tsv` records the next boundary.

#### Session 10 — Project Plan UI

**Status:** Complete — 2026-07-10. The Project panel now consumes one immutable
snapshot and renders one Project Plan: a Continue card, the six fixed textual
stages, a recursively traversed item hierarchy, and one contextual Library add
action. Legacy Work Order and duplicated asset sections are removed. Ordinary
rows emit one exact `ProjectItemRef` activation intent; repair and informational
rows cannot masquerade as normal activation. Notes, save, export, and close are
collapsed into secondary Project Details.

- replace Work Order plus duplicate legacy sections with one tree;
- add Continue card and prominent Add from Design Library;
- route all item types through a generic activation intent;
- move secondary notes/details out of primary navigation.

**Gate:** every visible project item is actionable or explicitly informational; no duplicate project asset lists remain.

Evidence: all 21 isolated Project Plan tests, 9 persistence-adapter tests, and
4 Project Plan UI architecture guards pass. The application builds; 1,391
full-binary tests across 152 suites and all 1,526 sequential CTest cases pass;
`git diff --check` and the source-size policy pass across 455 production files
with ten existing target warnings. Session 10 production units are at or below
233 lines and `build/source-size/session-11-start.tsv` records the next boundary.

### Phase 45 — Modular Prepare Carve

**Goal:** preparation is pinned to a project and decomposed into independently testable modules.

#### Session 11 — Project pinning and implicit-project removal

**Status:** Complete — 2026-07-10. Preparation now begins with an immutable
project/model-source/operation pin and token. The pure identity policy emits a
typed create-project requirement when no project exists and no mutation command
when disabled, invalid, foreign, or stale. Direct Carve persists only the exact
pinned operation and attaches Tool, Zeroing, and G-code children to that exact
parent. The old model-name/path directory request and its hidden temporary-
project creation/activation are removed; unpinned preparation stays export-only.
Unsaved semantic edits use the ProjectSession preparation lock, and Save must
persist the exact preparation before a project switch can commit.

- capture project/model/operation IDs when setup starts;
- replace implicit `ensureProjectForModel()` identity changes with an explicit create-project intent;
- ensure generated output attaches to the initiating operation;
- confirm or reject project switching during unsaved setup.

**Gate:** cross-project contamination tests pass; no setup path silently activates another project.

Evidence: 16 isolated identity/boundary tests, 5 application-adapter tests, 3
application architecture guards, exact-row repository tests, and preparation-
save transition tests pass. The application builds; 1,420 full-binary tests
across 156 suites and all 1,571 sequential CTest cases pass. `git diff --check`
and source-size policy pass across 461 production files with ten existing target
warnings; all new/split Session 11 units are at or below 493 lines and
`build/source-size/session-12-start.tsv` records the next boundary.

#### Session 12 — Preparation controller and stage order

**Status:** Complete — 2026-07-10. `PrepareCarveFlow` now owns the exact pinned
preparation sequence, tri-state readiness/blockers, monotonic semantic revisions,
stale async preview/save rejection, and the typed preparation-ready boundary.
Material & Blank hard-gates Choose Tool. Direct Carve adapts live facts into the
flow, dispatches Begin/Refresh/Open/Continue/Preview/Save/End intents, and cannot
enter Machine Setup without the exact `PreparationReady` effect. Standalone
Advanced preparation remains visibly unpinned and export-only. Navigation and
live-flow adaptation moved out of the main panel without changing toolpath
algorithms.

- introduce PrepareCarveFlow;
- change visible order to Design & Size, Material & Blank, Choose Tool, Carve Preview;
- move state/policy out of `DirectCarvePanel`;
- preserve existing toolpath algorithms.

**Gate:** setup controller is testable headlessly; Direct Carve panel line count drops materially.

Evidence: all 33 isolated carve-preparation tests and 4 UI architecture guards
pass. The application builds; 1,443 full-binary tests across 158 suites and all
1,611 sequential CTest cases pass. `git diff --check` and source-size policy pass
across 465 production files with ten existing warnings. Flow/navigation/adapter
units are at or below 437 lines; the main panel fell from 2,756 to 2,464 lines;
`build/source-size/session-13-start.tsv` records the next boundary.

#### Session 13 — Preparation step views

**Status:** Complete — 2026-07-10. Design & Size, Material & Blank, Choose
Tool, and Carve Preview are focused units below 500 lines. Pinned screens derive
beginner rationale and next guidance from immutable step facts tied to the exact
preparation pin and flow snapshot. Recommendations explain their reason;
advanced feed/toolpath and preview detail remain available through progressive
disclosure. Standalone Advanced preparation retains its export-only path.

- extract design/size, material/blank, tool, and preview views;
- each view consumes a snapshot and emits intents only;
- add recommendation explanations and progressive disclosure;
- retain Advanced settings access.

**Gate:** no step file exceeds 500 lines; visual workflow works at required UI scales.

Evidence: 35 focused flow/guidance/boundary/UI tests pass. The application
builds; 1,451 full-binary tests across 159 suites and all 1,625 sequential CTest
cases pass. `git diff --check` and source-size policy pass across 472 production
files with ten existing warnings. Step units are 249–335 lines, the shared
guidance renderer is 32 lines, `direct_carve_panel.cpp` fell from 2,464 to 1,483
lines, and `build/source-size/session-14-start.tsv` records the next boundary.

### Phase 46 — Protected Run Boundary

**Goal:** machine execution receives an immutable setup and owns the highest-priority safety lock.

#### Session 14 — RunPackage and RunCoordinator

**Status:** Complete — 2026-07-10. `dw_run_coordination` owns the immutable
handoff and execution state machine. The application adapter acquires the exact
ProjectSession run lock before submitting the exact generated program, records
job history, forwards progress and terminal failures, and releases the lock on
every terminal path.

- define immutable run handoff identity and preflight result;
- route start/pause/resume/abort through explicit commands;
- reject project/setup changes during a live stream;
- preserve export/preview when Run is disabled.

**Gate:** enabled/disabled and conflict-priority tests pass; no CNC command originates from PrepareCarveFlow.

Evidence: package identity includes the preparation pin, operation/G-code item,
semantic revisions, file fingerprint, and preflight facts. Start, pause, resume,
abort, progress, completion, and failure are closed typed commands/effects;
priority is Abort, Fail, Pause, Resume, Complete, Progress, Start. Disabled,
stale, mismatched, duplicate-start, and failed-preflight paths are inert. The
legacy false Streaming label was replaced with a real `CarveJob` submission,
and simulator tests prove G-code reaches the controller. Stream control moved
to a 170-line owned unit and the controller shell fell to 1,235 lines.

#### Session 15 — Machine Setup and Review & Run views

**Status:** Complete — 2026-07-10. Machine connection/homing, work zero, and
outline check are grouped as three visibly numbered Machine Setup views. Review
& Run names every missing fact, requires stock-secured/work-area confirmation,
and can enter Run CNC only through the protected preflight handoff.

- group connection, homing, zero, and outline into Machine Setup;
- add plain-language final safety summary;
- enter Run CNC only through explicit preflight success;
- return completed run to project history.

**Gate:** safety behavior is preserved; Direct Carve orchestration and step files meet size limits.

Evidence: active Run renders explicit Streaming, Paused, Complete, Aborted, and
Failed states plus keyboard-nav pause/resume/hold-to-abort controls. Completion
returns to Project history. Machine, zero, outline, review, and active-run units
are 94–442 lines; the Direct Carve shell is 647 lines and its obsolete monolith
cap is gone.

### Phase 47 — Layout, Viewport, and Advanced Compatibility

**Goal:** ship the new default without breaking saved layouts or experienced-user workflows.

#### Session 16 — Versioned layout/config migration

**Status:** Complete — 2026-07-10. Guided Workshop, Advanced Workbench, and CNC
Sender have stable built-in IDs. Version-one migration is idempotent, maps the
legacy Workshop built-in to Advanced, preserves custom presets byte-for-byte,
and keeps Guided opt-in until novice acceptance.

- add idempotent Guided layout migration;
- preserve custom presets;
- keep stable window aliases;
- extract layout/preset migration from `config.cpp`;
- expose Guided/Advanced selection without duplicating state.

**Gate:** old built-ins migrate once, custom presets survive, rerunning migration is a no-op.

Evidence: 21 focused migration/navigation tests pass. Experience selection uses
the existing ProjectWorkshopController and shared repositories rather than a
second mode model. Layout persistence moved to `config_layout.cpp`; `config.cpp`
fell to 718 lines and its legacy size cap was removed.

#### Session 17 — Viewport decomposition and preview identity

**Status:** Complete — 2026-07-10. Viewport shell, toolbar, interaction/ViewCube,
overlays, and G-code layer are separately owned while preserving the existing
dirty camera/navigation improvements. A pure presentation contract distinguishes
Project item content from temporary Library preview content.

- split viewport toolbar, interaction/view-cube, and overlays from the panel shell;
- preserve render output and current dirty navigation improvements;
- display project versus Library-preview identity without embedding project policy in rendering.

**Gate:** viewport behavior and visual checks match baseline; no target file exceeds 650 lines.

Evidence: shell 323 lines, toolbar 266, interaction 544, overlays 295, G-code
layer 522, header 236. Forty-six focused binary tests and 50 focused CTest cases
pass; fit/recenter, quarter turns, navigation styles, stale-fit behavior, and
render output remain covered. No viewport target exceeds 650 lines.

### Phase 48 — Integrated Validation and Release Readiness

**Goal:** prove the new experience with automated and novice validation before it becomes default.

#### Session 18 — End-to-end verification and handoff

**Status:** Engineering and responsive visual validation complete / human
release gate pending — 2026-07-11. Automated integration, accessibility
heuristics, the independently reviewed 72-state matrix, Linux packaging,
source health, version/README, and the exact novice-study handoff are complete.
Guided remains opt-in because the five qualifying people have not been run.

- run create/add/preview/return/prepare/close/reopen automated flows;
- validate 1366x768 and 4K at 100%, 150%, and 200% UI scale;
- validate keyboard navigation and text/icon status independent of color;
- run the full suite, static checks, packaging smoke checks, and file-size audit;
- conduct or prepare the five-person novice usability script;
- enable Guided as default only when release gates pass;
- update README, screenshots, version, and planning state.

**Gate:** 11/12 Definition of Done items are proven. Final Guided-default
acceptance remains held until the documented human gate passes with real raw
evidence.

Evidence: the canonical River Sign service-level regression covers
create/add/preview/return/prepare/close/reopen with both cross-project isolation
and durable identity. The full binary passes 1,587 tests across 180 suites;
sequential CTest passes 1,812/1,812. Source-size policy passes 524 files with no
new hard-ceiling violation, and `git diff --check` is clean. The compile-gated
real app passes all 72 states at 1366 x 768 and 3840 x 2160 with 100%, 150%, and
200% UI scale; three read-only reviews checked every original. Final TGZ and
`.run` artifacts install GraphQLite and complete all package smoke assertions.
Exact commands, hashes, screenshots, limitations, and the held-default decision
are recorded in `PROJECT-CENTERED-WORKSHOP-RELEASE-VALIDATION.md`; the
no-coaching P01–P05 protocol and raw scoring sheets are in
`PROJECT-CENTERED-WORKSHOP-USABILITY.md`.
