---
gsd_state_version: 1.0
milestone: v0.7.0
milestone_name: Project-Centered Workshop
status: release-gate-pending
stopped_at: Sessions 01-17 complete; Session 18 engineering and 72-state visual pass; Guided default held for human acceptance
last_updated: "2026-07-11T08:36:31-07:00"
last_activity: 2026-07-11
progress:
  total_phases: 9
  completed_phases: 8
  total_sessions: 18
  completed_sessions: 17
---

# Project State

## Current Focus

- **Milestone:** v0.7.0 Project-Centered Workshop
- **Status:** Engineering and responsive visual candidate complete — human Guided-default gate pending
- **Master plan:** `.planning/PROJECT-CENTERED-WORKSHOP-PLAN.md`
- **Session plan:** `.planning/PROJECT-CENTERED-WORKSHOP-SESSIONS.md`
- **Requirements:** `.planning/REQUIREMENTS.md`
- **Roadmap:** `.planning/ROADMAP.md`

## Core Outcome

An inexperienced CNC hobbyist can remain oriented inside one project from selecting a reusable Library design through preparation and a protected machine run.

Project is persistent context. Design Library is a catalog/picker. Run CNC is a deliberate safety boundary. Advanced Workbench remains available but shares the same project/session truth.

## Current Position

- **Phase:** 48 — Integrated Validation and Release Readiness
- **Session:** 18 — End-to-end verification and handoff
- **Implementation status:** v0.7.0 code, automation, 72/72 responsive workflow captures, Linux packages, and handoff are complete; retain Advanced as default until the five-person novice gate passes

Session 01 evidence is recorded in
`.planning/PROJECT-CENTERED-WORKSHOP-BASELINE.md`:

- the 22 modified and six untracked pre-existing source paths are grouped by
  ownership, fingerprinted, and backed up locally without stashing or resetting;
- 1,154 tests in 128 suites pass with zero failures;
- isolated diagnostic initialization succeeds;
- Home, Library/Project, Direct Carve/Sender, and Run/Sender screens are captured;
- current oversized-file counts and navigation behavior are recorded;
- three deterministic River Sign STL designs and a moderated task are available.

Session 02 established the architecture ratchet:

- `dw_workshop_core` is a standard-library-only static target linked by the app
  and tests;
- Guided and Advanced commands route to one `WorkshopCommandTarget`;
- disabling Guided rejects before dispatch and cannot mutate shared context;
- an isolated 11-test executable links no ImGui, OpenGL, database, managers, or
  application code;
- 1,165 tests in the full binary pass;
- all 21 legacy production monoliths have non-increasing size caps;
- the pre-Session-03 snapshot is
  `build/source-size/session-03-start.tsv` (379 production files).

Session 03 implemented the authoritative policy boundary without visible UI or
persistence changes:

- active project, project item, Library preview, Library return, Run return,
  dirty/setup state, explicit Run identity, and context generation have one owner;
- Library preview overlays and restores the exact underlying project item;
- project and external runs are explicitly distinct and generically navigating
  into Run is rejected;
- active Run blocks project, item, Library, and preparation mutations;
- dirty plus preparation blockers are presented together and resolved atomically
  through a unique confirmation token;
- stale generations, stale confirmations, and mismatched Run completion are
  rejected without mutation;
- 25 isolated ProjectSession tests and all 1,190 full-suite tests pass;
- production files are 385, 64, and 257 lines, all below the 500-line target.

Session 04 integrated that boundary into the application and hardened project
persistence:

- New, Open, Recent, folder import, archive import, Close, temporary-project
  decisions, and Direct Carve implicit creation use one activation gateway;
- asynchronous model completions carry both session generation and project
  identity, so an old load cannot overwrite a newer context;
- portable project folders hydrate through a transaction-only importer and
  adopt their local database identity on first save;
- temporary ownership is persisted in schema v18 and protected by an owned
  marker, collision-safe promotion, and database-first discard;
- project manifests and ownership markers use atomic writes, while failed saves
  restore the prior in-memory, database, manifest, and directory state;
- lifecycle/storage/importer behavior passes the full 1,226-test suite, the
  isolated 25-test ProjectSession suite, and the isolated 11-test workshop suite;
- the source-size policy passes across 395 production files with no cap growth;
  `build/source-size/session-05-start.tsv` is the next-session snapshot;
- the clean-profile diagnostic initializes in 297.42 ms; its three known
  `tools.vtdb` duplicate-column warnings are unchanged.

Session 05 established the persistent project shell and its application seam:

- `ProjectWorkshopController` routes typed Back, select, clear, and Library
  preview intents into the same ProjectSession command target used by both
  experiences;
- the shared context bar projects immutable project, dirty, active-item or
  preview, area, and machine facts without depending on project managers,
  repositories, controllers, or hardware;
- Project and Library selection changes are veto-capable, so rejected
  transitions cannot leave panel highlight state ahead of session truth;
- active-item removal clears the authoritative session selection first and
  machine/run locks protect both removal and navigation;
- workshop application composition and persistent-shell rendering moved into
  owned files, reducing `application_wiring.cpp` to 971 lines and
  `ui_manager.cpp` to 480 lines;
- 1,239 tests across 137 suites pass, including 37 focused project/session
  tests and 12 workshop boundary tests;
- the source-size policy passes across 402 production files with ten ratcheted
  legacy warnings and no cap growth;
- visual checks at 1600x900 and 1000x700 show the bar without dock/status
  overlap, and clean-profile initialization succeeds in 288.94 ms.

Session 06 consolidated project entry and made restart state truthful:

- Home is the single New, Open, and Recent surface and hides unrelated empty
  workshop panels; imports and Library browsing use explicit authoritative
  routes instead of leaving ProjectSession and visible layout out of sync;
- named creation is an owned prepare/activate/publish transaction with typed,
  retryable failure and rollback outcomes, so it cannot delete unrelated data;
- a dedicated versioned resume sidecar stores stable ProjectId and
  ProjectItemId values atomically, validates the complete storage identity on
  both sides of activation, and clears on explicit or destructive close;
- model, G-code, and Direct Carve restoration report pending until real content
  opens, and exact route/item guards prevent stale asynchronous commits;
- project transitions clear all Direct Carve project state and reject while
  streaming, outlining, zeroing, or running;
- 1,297 tests across 143 suites pass, all 1,371 CTest cases pass, and the two
  isolated boundaries pass 36 workshop and 38 ProjectSession cases;
- the source-size policy passes across 417 production files with ten ratcheted
  legacy warnings and no cap growth; `build/source-size/session-07-start.tsv`
  is the next-session snapshot;
- clean Home, named creation, explicit close, and restart resume were checked at
  1600x900, while the intended compact launch is complete and readable at
  1000x700; clean-profile initialization completes in 283.37 ms.

Session 07 isolated the contextual Design Library policy:

- `LibraryPickerFlow` owns explicit Manage Library, Start Project, and Add to
  Project purposes without UI, database, manager, renderer, or filesystem
  dependencies;
- temporary selection neither previews nor mutates membership; Preview, Start,
  Add, and Restore are separate typed requests with monotonic ownership tokens;
- Add is pinned to ProjectId, emits unique missing-only assets, suppresses
  duplicate submission, and keeps partial failures retryable;
- Restore uses request/ack state plus the expected ProjectSession generation and
  exact route/project/item verification, so stale or look-alike state cannot
  consume an old return request;
- imports are offered back into the active temporary selection without implicit
  Preview, Add, or Start behavior;
- 26 direct tests pass in both the full and isolated targets; 1,323 tests across
  144 suites and all 1,423 sequential CTest cases pass;
- the source-size policy passes across 420 production files with ten ratcheted
  warnings; the three module units are 221, 346, and 214 lines, and
  `build/source-size/session-08-start.tsv` records the next-session boundary.

Session 08 completed the contextual Design Library experience and persistence
adapters:

- Home, View, and Project entry points open one visible purpose-specific picker
  with explicit Preview, Start Project, Add to Project, and acknowledged return;
- one pinned membership service atomically ensures mixed Model/G-code assets,
  reports already-member truth, and cannot redirect work into another project;
- imported IDs return to the initiating picker purpose without implicit preview
  or membership, and cancellation clears stale import intent;
- source deletion preflights the full batch, blocks active previews and links from
  current or closed projects, names those projects, and clears only confirmed
  deletions, including truthful partial outcomes;
- Library is now a transient workflow surface rather than saved layout state;
  panel X, menu close, Home, Open, context-bar Back, and Sender switching cannot
  strand a hidden picker;
- model preview defers focus/material state until its token is current, suppresses
  preview-only persistence, and protects replacement loads from a UI-thread join;
- Library presentation, list layout, thumbnails, panel actions, workflow
  adapters, coordinator, and application composition now live in owned files;
  the largest newly or substantially edited Library/application unit is 499 lines;
- the app builds and initializes against the live OpenGL/database path, 1,357
  tests across 147 suites pass, all 35 isolated picker tests and all 1,471
  sequential CTest cases pass, and the
  source-size policy passes across 440 production files with ten legacy target
  warnings; `build/source-size/session-09-start.tsv` records the next boundary.

Session 09 established Project Plan as a deterministic navigation authority:

- `dw_project_plan` accepts normalized immutable items plus tri-state operation
  evidence and an identity-matched live run snapshot; it has no persistence,
  UI, renderer, manager, filesystem, JSON, or hardware dependency;
- canonical flat nodes and child IDs preserve a true hierarchy independent of
  repository ordering, while missing parents and cycles become visible typed
  diagnostics instead of dropped or recursive rows;
- the fixed stages are Design & Size, Material & Blank, Choose Tool, Carve
  Preview, Machine Setup, and Review & Run; unknown fit/machine evidence never
  becomes ready, and Material & Blank precedes Choose Tool;
- next-action precedence protects an active run, then repairs the earliest
  required stale/missing item, then advances the first incomplete stage;
  stale ancillary costs/history do not hijack preparation;
- imported G-code remains a supported branch without inventing a design
  requirement, and malformed live/saved job identity requires reconciliation;
- `project_repository.cpp` is now 270 lines, with open-item CRUD (339),
  synchronization (477), validation (218), and shared detail (170) in focused
  units; the obsolete 1,371-line cap is gone;
- all 17 isolated plan tests, 1,374 full-binary tests across 149 suites, and all
  1,505 sequential CTest cases pass; the application builds, `git diff --check`
  is clean, and source-size policy passes across 449 production files with ten
  existing target warnings; `build/source-size/session-10-start.tsv` records the
  next boundary.

Session 10 replaced duplicate Project navigation with the Project Plan:

- the panel consumes one immutable snapshot and owns no repository or source-
  specific selection policy;
- Continue, the six textual stage rows, one recursive hierarchy, and a deduped
  Add from Design Library action form the primary path;
- ordinary item activation validates the exact project/item pair, while repair
  remains a distinct truthful informational route;
- notes, save, export, and close are secondary Project Details;
- all 21 isolated Project Plan tests, 9 adapter tests, 4 UI architecture tests,
  1,391 full-binary tests across 152 suites, and all 1,526 sequential CTest cases
  pass; the app builds, `git diff --check` is clean, and source-size policy passes
  across 455 production files with ten existing warnings;
  `build/source-size/session-11-start.tsv` records the next boundary.

Session 11 pinned preparation to durable identity and removed hidden project
mutation:

- `dw_carve_preparation` owns immutable pins, revisions/tokens, normalized
  identity snapshots, and typed ready/create/invalid/stale decisions without UI,
  persistence, or CNC dependencies;
- explicit Continue may create one planned operation beneath the exact model;
  ordinary loads validate exact model source and operation parent before setup;
- setup saves update only the pinned row, and Zeroing, Tool, and generated G-code
  children use the pinned project/operation rather than current-project or name/
  hash inference;
- unpinned preparation is visibly standalone and export-only; no setup path can
  create or activate a project;
- semantic edits activate the ProjectSession preparation lock, and a project-
  switch Save resolution must persist preparation first;
- 1,420 tests across 156 suites and all 1,571 sequential CTest cases pass; the
  application builds, diff/source-size checks pass across 461 production files,
  and `build/source-size/session-12-start.tsv` records the next boundary.

Session 12 moved pinned preparation policy into `PrepareCarveFlow`:

- the pure flow owns Design & Size, Material & Blank, Choose Tool, and Carve
  Preview order, typed blockers, exact pin retention, and monotonic edit revision;
- stale preview/save completion cannot overwrite newer facts, and Continue from
  a current preview emits a typed preparation-ready effect with no CNC command;
- pinned UI navigation cannot skip incomplete stages; unpinned Advanced use
  remains an explicit standalone/export-only compatibility path;
- live adaptation and navigation are focused units at or below 433 lines, while
  the main Direct Carve panel fell from 2,756 to 2,464 lines;
- 1,443 tests across 158 suites and all 1,611 sequential CTest cases pass;
  `build/source-size/session-13-start.tsv` records the next boundary.

Session 13 completed the modular preparation views:

- Design & Size, Material & Blank, Choose Tool, and Carve Preview now live in
  focused view files between 249 and 335 lines, each below the 500-line gate;
- pinned views render title, rationale, and specific next guidance from an
  immutable `PreparationStepFacts` projection of the exact active pin and flow
  snapshot, while standalone Advanced preparation keeps its compatibility copy;
- the pure step-guidance contract provides typed field, selection, preview, and
  Advanced intents, recommendation rationale, blocker-specific next actions,
  retained disclosure state, and a disabled/no-action seam;
- material selection and beginner recommendations remain visible, while feed/
  toolpath tuning and detailed preview statistics are progressively disclosed;
- the main Direct Carve panel fell from 2,464 to 1,483 lines and its source-size
  cap was ratcheted to that exact count;
- 1,451 tests across 159 suites and all 1,625 sequential CTest cases pass; the
  application builds, `git diff --check` is clean, and the source-size policy
  passes across 472 production files with ten existing target warnings;
  `build/source-size/session-14-start.tsv` records the next boundary.

Session 14 completed the protected execution boundary:

- immutable `RunPackage` identity binds the exact preparation pin, operation,
  G-code item/revision/fingerprint, and preflight facts;
- `RunCoordinator` owns enabled/disabled behavior, lock-before-stream ordering,
  typed progress and terminal commands, and deterministic safety priority;
- the application effect adapter validates the durable hierarchy and file hash,
  submits real `CarveJob` G-code, records history, and cannot strand a run lock;
- controller stream control is an owned 170-line unit and simulator tests prove
  the program reaches the real submission boundary.

Session 15 completed the visible Machine Setup and Run views:

- connection/homing, work zero, and outline are three numbered Machine Setup
  screens followed by plain-language Review & Run;
- final entry requires explicit preflight plus stock/work-area confirmation;
- Streaming, Paused, Complete, Aborted, and Failed are textual states with
  keyboard-nav pause/resume/hold-to-abort controls;
- the Direct Carve shell is 647 lines and focused views are 94–442 lines.

Session 16 completed layout and experience migration:

- Guided Workshop, Advanced Workbench, and CNC Sender use stable IDs;
- version-one migration is idempotent, maps legacy Workshop to Advanced, and
  leaves custom presets unchanged;
- both experiences share ProjectSession and repositories; disabling Guided is
  inert and no second project mode exists;
- layout persistence moved out of `config.cpp`, which is now 718 lines.

Session 17 completed viewport decomposition:

- shell, toolbar, interaction/ViewCube, overlays, and G-code layers are owned
  units between 266 and 544 lines;
- a pure presentation contract distinguishes a Project item from a temporary
  Library preview without putting project policy in rendering;
- current camera/navigation work, fit/recenter, quarter-turn, and stale-fit
  behavior remain covered.

Session 18 completed the engineering and responsive visual candidate plus an
honest human-study handoff:

- the River Sign E2E proves create/add/preview/return/prepare/close/reopen and
  cross-project isolation using all three fixtures;
- visible keyboard focus and color-independent Prepare status were repaired and
  guarded; the exact no-coaching P01–P05 study is prepared but not fabricated;
- all six size/scale environments pass all 12 deterministic workflow states
  after independent review of the 72 original captures;
- TGZ and `.run` packages install `digital_workshop`, `dw_settings`, GraphQLite,
  and resources, then initialize cleanly in isolated profiles;
- fresh and legacy tool schemas migrate quietly and idempotently; the 1,006-line
  tool database split into 498-, 148-, 231-, and 170-line owned units;
- the full binary passes 1,587 tests across 180 suites, sequential CTest passes
  1,812/1,812, source-size policy passes 524 production files, and
  `git diff --check` is clean.

`AREA` reports the current surface while Project Plan reports the active
preparation stage. Advanced remains the release default until the Phase 48
human acceptance gate is actually run and passes.

The plan contains nine sequential phases and eighteen bounded sessions:

1. Phase 40 — Baseline and Module Guardrails
2. Phase 41 — Authoritative Project Session
3. Phase 42 — Home and Persistent Project Shell
4. Phase 43 — Contextual Design Library
5. Phase 44 — Project Plan Navigation
6. Phase 45 — Modular Prepare Carve
7. Phase 46 — Protected Run Boundary
8. Phase 47 — Layout, Viewport, and Advanced Compatibility
9. Phase 48 — Integrated Validation and Release Readiness

## Required Module Boundaries

- `ProjectSession` is the only owner of active project/context transitions.
- `LibraryPickerFlow` owns temporary browse/picker selection and explicit add/preview/cancel commands.
- `ProjectPlanBuilder` derives hierarchy, readiness, blockers, and next action without persistence or UI dependencies.
- `ProjectWorkshopController` routes UI intents to domain services without owning policy.
- `PrepareCarveFlow` owns editable preparation and cannot stream.
- `RunCoordinator` consumes immutable RunPackage, owns the run lock, and cannot mutate setup identity.
- Guided and Advanced experiences consume the same ProjectSession and repositories.

## De-Monolith Rules

- New and substantially rewritten files target 500 lines.
- No milestone-created file may exceed 750 lines.
- A touched file already above 750 lines must shrink in that phase.
- Extractions must move owned state/behavior behind a small contract; arbitrary multi-file class splitting does not count.
- Direct tests are required before callers migrate.
- Every session reports line counts for edited production files.

## Safety and Conflict Priority

1. Emergency stop and active-stream safety
2. Active Run lock
3. Unsaved preparation/project transition confirmation
4. Explicit project activation or switch
5. Project Plan selection
6. Temporary Library preview
7. Passive/default routing

## Prerequisite Before Source Changes

The current worktree contains pre-existing, unrelated changes involving:

- sidecar image thumbnails;
- viewport navigation and camera behavior;
- Direct Carve preview behavior.

Phase 40 Session 01 must preserve and explicitly checkpoint or otherwise record ownership of these changes. Do not reset, discard, stash blindly, or fold them invisibly into this milestone.

## Session Completion Gate

Every session must finish with:

- its direct module tests passing;
- the full test binary passing;
- `git diff --check` clean;
- edited production file line counts recorded;
- Advanced Workbench compatibility checked when relevant;
- public contract/dependency changes documented;
- a written handoff naming the exact next session.

## Scope Exclusions

- no new toolpath algorithms;
- no CNC protocol changes;
- no FreeCAD integration;
- no costing, optimizer, or AI feature expansion;
- no cloud, accounts, or telemetry;
- no repository-wide refactor of unrelated oversized files;
- no per-project global material/transform redesign;
- no removal of existing external G-code or Advanced workflows.

## Next Action

Conduct the exact five-person River Sign protocol in
`.planning/PROJECT-CENTERED-WORKSHOP-USABILITY.md`. Record raw participant data
without coaching or substitution, evaluate every formula, and change the
built-in default to Guided only if the human gate passes. Engineering, visual,
and package evidence is in
`.planning/PROJECT-CENTERED-WORKSHOP-RELEASE-VALIDATION.md`.

---
*v0.7.0 engineering and 72-state visual candidate completed: 2026-07-11; human release gate pending*
