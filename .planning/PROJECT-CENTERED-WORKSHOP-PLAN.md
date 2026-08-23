---
milestone: v0.7.0
name: Project-Centered Workshop
status: ready-for-execution
created: 2026-07-10
phases: 9
planned_sessions: 18
requirements: .planning/REQUIREMENTS.md
roadmap: .planning/ROADMAP.md
session_plan: .planning/PROJECT-CENTERED-WORKSHOP-SESSIONS.md
---

# Project-Centered Workshop — Scoped Multi-Session Plan

## Outcome

Digital Workshop will guide an inexperienced CNC hobbyist through one persistent project context from design selection to a safe machine run.

The redesign removes the accidental "Library mode versus Project mode" mental model:

- **Home** starts or resumes work.
- **Project** is the persistent preparation context.
- **Design Library** supplies reusable source assets to a project.
- **Run CNC** is the protected execution context.
- **Advanced Workbench** preserves the existing freeform docked tools for experienced users.

The milestone is complete when a novice can start a project from a Library design, add and preview other designs without losing context, prepare a carve, enter Run CNC deliberately, close the app, and resume the same project without navigation coaching.

## Why This Is a Structural Milestone

The current behavior is distributed across independent panel state:

- Library and Project are visible together and both drive global model focus.
- Library preview, project selection, viewport focus, and Direct Carve each retain overlapping selection state.
- Project renders a Work Order and duplicate legacy asset sections.
- Direct Carve preparation is classified as a Sender surface and can implicitly create or replace the active project.
- saved layouts can reintroduce the old navigation even after defaults change.

Rearranging panels without correcting ownership would preserve the confusion. This plan therefore establishes authoritative session state first, then changes presentation.

## Product Contract

The default guided experience must always answer:

1. What project is active?
2. What project item is active?
3. Is the visible design a project item or only a Library preview?
4. What preparation step is current?
5. What is the next useful action?
6. Is the machine safe and ready to run?

## Target Journey

```text
Home
  -> Start with a design
  -> Project: Design & Size
  -> Material & Blank
  -> Choose Tool
  -> Carve Preview
  -> Machine Setup
  -> Review & Run
  -> Run CNC
  -> Completed Job / Project History
```

Library browsing from a project is a temporary picker route with a persistent `Back to <project>` action. It is never a competing project context.

## Scope

### Included

- one authoritative project-session and active-selection model;
- Home consolidation for New, Open, Recent, and Start from Design;
- persistent project context bar;
- Design Library browse, preview, and project-picker purposes;
- one Project Plan tree and derived next action;
- explicit project pinning for Direct Carve setup and output;
- separation of Prepare Carve from protected Run CNC behavior;
- Guided and Advanced experience seams sharing the same state;
- versioned layout and configuration migration;
- targeted decomposition of files touched by the milestone;
- automated session, navigation, persistence, migration, and run-handoff tests;
- novice usability and accessibility validation.

### Explicitly excluded

- new CAM strategies or toolpath algorithms;
- CNC protocol or firmware behavior changes;
- Windows serial/TCP implementation;
- cloud sync, accounts, or telemetry;
- accounting features and new costing behavior;
- global undo/redo;
- a full repository-wide rewrite of every oversized file;
- per-project model-material storage redesign;
- per-project transform overrides beyond existing operation intent;
- removal of the Advanced Workbench;
- broad theme or visual-brand redesign.

The global model material remains a shared Library attribute during this milestone. Project-specific material and stock choices remain operation/setup intent. The UI must not claim otherwise.

## Architectural Rules

1. One module owns each behavior and its state.
2. Views receive immutable snapshots and emit explicit intents.
3. New code must not add panel-to-panel business logic.
4. `Application` and `UIManager` route events; they do not own workflow policy.
5. Only `ProjectSession` may activate, replace, or close the active project.
6. Library preview cannot mutate project membership or active project selection.
7. Prepare Carve cannot issue stream commands.
8. Run coordination cannot silently change project, model, stock, tool, or setup identity.
9. Active CNC streaming has highest navigation priority and locks project identity.
10. Every new module is testable without ImGui, OpenGL, filesystem dialogs, or hardware.
11. Every touched monolithic file must get smaller or remain below the milestone ceiling.
12. No session mixes unrelated UX, refactor, and release work.

### Module packaging convention

New behavior is organized as vertical modules under `src/modules/<responsibility>/`, not scattered as policy across `app`, `managers`, and `ui`.

Each module may contain:

```text
public contract and immutable snapshots
internal controller/state implementation
adapters to existing repositories or services
optional ui/ views that consume snapshots and emit intents
direct tests at the public boundary
```

Cross-module callers include public contracts only. `Application` remains the composition root that registers modules and adapters. Omitting an optional module from composition must remove its routes and commands without leaving hidden state mutations.

## Proposed Module Boundaries

### 1. ProjectSession

**Owning files**

```text
src/modules/project_session/project_context.h
src/modules/project_session/project_session_commands.h
src/modules/project_session/project_session.h
src/modules/project_session/project_session.cpp
```

**Consumes**

- activate/open/create/close project intents;
- select project item;
- begin/cancel Library preview;
- enter/leave Library picker;
- enter Prepare or Run route;
- current dirty/setup/stream locks.

**Returns**

- explicit `SessionTransition` result;
- `ContextChanged` snapshot;
- `Rejected` with a typed reason;
- generation token for stale asynchronous-load rejection.

**Owns**

- active project ID;
- active project item/source IDs;
- selection origin;
- optional Library preview and return selection;
- current route;
- session generation;
- preparation and run locks.

**Enable/disable seam**

ProjectSession is foundational and always enabled. Guided and Advanced experiences both consume it, preventing divergent project truth.

**Conflict priority**

`ActiveStream > UnsavedSetup > ProjectSwitch > LibraryPreview > ordinary navigation`.

**Boundary tests**

- activate/switch/close;
- preview/cancel/restore;
- stale generation rejection;
- streaming lock;
- rejected transition reasons.

### 2. LibraryPickerFlow

**Owning files**

```text
src/modules/design_library/library_picker_flow.h
src/modules/design_library/library_picker_flow.cpp
src/modules/design_library/ui/library_picker_view.h
src/modules/design_library/ui/library_picker_view.cpp
```

**Consumes**

- ProjectSession snapshot;
- browse purpose: manage, start-project, or add-to-project;
- selected Library IDs;
- preview/add/cancel/import-complete intents.

**Returns**

- preview request;
- start-project request;
- idempotent add-assets request;
- cancel/restore request;
- visible action labels containing the active project name.

**Owns**

- picker purpose;
- temporary multi-selection;
- preview identity;
- return context.

**Enable/disable seam**

Rendered by Guided experience. Advanced Workbench may open the existing Library panel, but both use the same add/preview commands.

**Boundary tests**

- preview never changes membership;
- add is explicit and idempotent;
- cancel restores project selection;
- imported IDs are offered to the active picker purpose.

### 3. ProjectPlanBuilder

**Owning files**

```text
src/modules/project_plan/project_plan.h
src/modules/project_plan/project_plan.cpp
src/modules/project_plan/project_plan_detail.h
src/modules/project_plan/project_plan_detail.cpp
```

**Consumes**

- immutable project record;
- project open items;
- typed persisted operation evidence and live operation/run snapshots.

**Returns**

- canonical flat node storage with true parent/child IDs and roots;
- six beginner-facing preparation stages;
- stage readiness and blockers;
- one next-action command and explanation.

**Owns**

- no persistence;
- no panel state;
- deterministic projection only.

**Enable/disable seam**

Guided UI renders the projection. Advanced panels may ignore it without affecting project data.

**Boundary tests**

- empty/new project;
- stale/missing items;
- parent/child ordering;
- next-action precedence;
- complete project and completed job.

### 4. ProjectWorkshopController

**Owning files**

```text
src/modules/workshop/project_workshop_controller.h
src/modules/workshop/project_workshop_controller.cpp
src/modules/workshop/ui/project_context_bar.h
src/modules/workshop/ui/project_context_bar.cpp
src/app/application_wiring_workshop.cpp
```

**Consumes**

- UI intents;
- ProjectSession transitions;
- repository/service responses.

**Returns**

- view snapshots;
- explicit commands to existing managers;
- notifications and typed errors.

**Owns**

- orchestration only;
- no presentation policy;
- no repository data duplicated from source modules.

**Enable/disable seam**

Guided experience routes through the controller. Advanced Workbench adapters call the same commands.

**Boundary tests**

- routing and command ownership;
- Guided disabled behavior emits no Guided-only commands;
- active project remains identical between experiences.

### 5. PrepareCarveFlow

**Owning files**

```text
src/modules/carve_preparation/prepare_carve_flow.h
src/modules/carve_preparation/prepare_carve_flow.cpp
src/modules/carve_preparation/ui/direct_carve_panel.h
src/modules/carve_preparation/ui/direct_carve_panel.cpp
src/modules/carve_preparation/ui/design_size_step.cpp
src/modules/carve_preparation/ui/material_blank_step.cpp
src/modules/carve_preparation/ui/tool_step.cpp
src/modules/carve_preparation/ui/preview_step.cpp
```

**Consumes**

- pinned project/model/operation IDs;
- model, material, stock, tool, and machine snapshots;
- existing Direct Carve workflow state;
- setup intents.

**Returns**

- updated setup snapshot;
- project open-item update commands;
- toolpath generation request;
- immutable `RunPackage` handoff request.

**Owns**

- preparation state only;
- project pin captured at setup start;
- no machine stream lifecycle.

**Enable/disable seam**

Direct Carve can remain hidden or disabled without affecting Library, ProjectSession, imported G-code, or Advanced sender workflows.

**Boundary tests**

- no-project rejection with create-project action;
- project/model pin remains stable;
- material precedes tool recommendation;
- output attaches to initiating operation;
- disabled flow emits no machine or project mutation commands.

### 6. RunCoordinator

**Owning files**

```text
src/modules/cnc_run/run_package.h
src/modules/cnc_run/run_coordinator.h
src/modules/cnc_run/run_coordinator.cpp
src/modules/cnc_run/ui/machine_setup_step.cpp
src/modules/cnc_run/ui/review_run_step.cpp
```

**Consumes**

- immutable `RunPackage`;
- machine status snapshot;
- start/pause/resume/abort/complete intents.

**Returns**

- explicit commands for existing CNC/streamer services;
- run status and recovery guidance;
- job-history completion command.

**Owns**

- run lock and run identity;
- no project setup mutation.

**Enable/disable seam**

When unavailable, Prepare remains usable through preview/export. No stream command is emitted.

**Conflict priority**

Emergency stop and active-stream safety override every navigation or setup command.

**Boundary tests**

- immutable handoff identity;
- start prerequisites;
- pause/resume/abort routing;
- project switch rejected during stream;
- disabled coordinator emits no CNC commands.

### 7. ViewportPresentation

**Owning files**

```text
src/modules/viewport/viewport_content_store.h
src/modules/viewport/viewport_content_store.cpp
src/modules/viewport/viewport_presenter.h
src/modules/viewport/viewport_presenter.cpp
src/modules/viewport/ui/viewport_panel.h
src/modules/viewport/ui/viewport_panel.cpp
src/modules/viewport/ui/viewport_toolbar.cpp
src/modules/viewport/ui/viewport_interaction.cpp
src/modules/viewport/render/model_layer.cpp
src/modules/viewport/render/gcode_layer.cpp
src/modules/viewport/render/simulation_layer.cpp
```

**Consumes**

- explicit show-project-item, show-Library-preview, restore-project-item, and clear commands;
- mesh/G-code/toolpath content;
- camera and render settings;
- ProjectSession identity only as presentation labels supplied by Workshop.

**Returns**

- camera/navigation intents;
- viewport interaction events;
- render/preview status.

**Owns**

- loaded visual content and camera state;
- toolbar, navigation, view-cube, overlay, and render-layer state;
- no project membership, Library policy, or project switching.

**Enable/disable seam**

Viewport rendering is core application presentation. Optional overlay layers can be omitted independently and must emit no render or domain commands when disabled.

**Boundary tests**

- project item versus temporary preview commands;
- restore prior presentation after preview;
- stale content generation rejection;
- optional layer disabled behavior;
- camera/navigation behavior parity after extraction.

## Guided and Advanced Experience Seam

```text
ProjectSession + domain modules
             |
      -----------------
      |               |
 Guided Workshop   Advanced Workbench
 project shell     existing dock panels
 step navigation   direct panel access
      |               |
      ------ same commands/repositories ------
```

`ExperienceMode::Guided` becomes the release default only after Phase 48 human
acceptance. `ExperienceMode::Advanced` preserves freeform panels. This is not
two data models or two project modes.

## De-Monolith Policy

### File rules

- target for new or substantially rewritten files: **500 lines or fewer**;
- milestone hard ceiling: **750 lines**;
- generated font data is exempt;
- a touched file already above 750 lines must end the phase smaller;
- extraction must move owned state and behavior behind a contract, not only split one class across arbitrary translation units;
- every extraction gets direct unit tests before callers migrate;
- no new circular dependency between `core`, `app`, `managers`, and `ui`.

### Targeted baseline and destination

| Current file | Baseline | Planned destination |
|---|---:|---|
| `src/ui/panels/direct_carve_panel.cpp` | 3,369 | thin panel/orchestrator plus step modules; no file over 600 |
| `src/ui/panels/viewport_panel.cpp` | 1,860 | shell, interaction, toolbar, and overlay/render modules; no file over 650 |
| `src/app/application_wiring.cpp` | 1,081 | workshop/project wiring extracted; original under 750 |
| `src/core/database/project_repository.cpp` | 1,381 | project CRUD and open-item implementation separated; each under 750 |
| `src/core/config/config.cpp` | 1,027 | layout/preset migration extracted; original under 750 |
| `src/core/library/library_manager.cpp` | 1,092 | no broad rewrite; extract only Library/project interaction touched by this milestone |
| `src/ui/panels/library_panel.cpp` | 776 | shared browser shell and picker view separated; each under 600 |
| `src/ui/panels/library_panel_items.cpp` | 761 | item presentation and action policy separated; each under 600 |

Other oversized files are outside this milestone unless the selected workflow cannot be completed without touching them.

### Build boundary

Start with a small `dw_workshop_core` aggregation target, then expose focused render-independent targets (`dw_project_session`, `dw_project_plan`, and preparation/run contracts) as their boundaries stabilize. The application and tests link the same targets. Do not migrate the entire repository into new CMake targets in one session.

## Cross-Session Execution Protocol

Every implementation session must:

1. begin from the prior session summary and verify the worktree scope;
2. list pre-existing dirty files before editing;
3. change one behavior boundary or one extraction boundary;
4. add or update direct tests in the same session;
5. run targeted tests, the full test binary, and `git diff --check`;
6. report line counts for every edited production file;
7. stop if an edited file exceeds 750 lines without an approved extraction plan;
8. record the public module contract and any changed dependency;
9. preserve Advanced Workbench behavior unless that session explicitly migrates it;
10. produce a short session summary and a clean handoff for the next session.

No session should depend on uncommitted, undocumented assumptions from an earlier session.

## Prerequisite: Preserve Current Work

The repository already contains unrelated uncommitted work in three themes:

- sidecar image thumbnails;
- viewport navigation/camera behavior;
- Direct Carve preview behavior.

Before Phase 40 implementation, intentionally checkpoint these themes separately or otherwise record their exact ownership. The UX milestone must not overwrite or silently absorb them.

## Phase and Session Plan

The milestone is divided into nine phases and eighteen bounded execution sessions. Detailed objectives, dependencies, and gates are maintained in [PROJECT-CENTERED-WORKSHOP-SESSIONS.md](PROJECT-CENTERED-WORKSHOP-SESSIONS.md).

Execution remains sequential from Phase 40 through Phase 48. Limited parallel work is allowed only in isolated worktrees after the owning public contract is frozen and only when file ownership does not overlap.

## Automated Verification Matrix

| Area | Required tests |
|---|---|
| ProjectSession | activate, switch, close, preview, restore, locks, stale generation |
| Library picker | purpose, preview isolation, idempotent add, cancel, import handoff |
| Project plan | hierarchy, readiness, warnings, next action, completion |
| Persistence | save, close, reopen, last active item, missing source recovery |
| Direct Carve | project pin, operation pin, material-before-tool, output ownership |
| Run boundary | immutable package, preflight, enabled/disabled, stream lock, abort |
| Layout migration | old built-in, custom preset preservation, idempotence |
| Experience seam | Guided and Advanced share project identity and repositories |
| Integration | one Library design used by two projects without cross-project mutation |

## Usability Release Gates

Using the `River Sign` task with five inexperienced hobbyists and no navigation coaching:

- 5/5 create a named project from a Library design;
- 5/5 add a second design;
- 5/5 preview a third design without accidentally adding it;
- zero assets or generated outputs attach to the wrong project;
- at least 90% identify the active project and preview/membership state within five seconds;
- median start-project-from-design time is under 60 seconds;
- median add-another-design time is under 30 seconds;
- Back to Project is one visible action from Library preview;
- continuing the active operation takes no more than two actions;
- no participant uses layout presets or hunts panel tabs to recover project context;
- average Single Ease Question score is at least 5.5/7.

## Migration and Rollback

- schema changes, if any, are additive and backward-compatible;
- ProjectSession activation ships before Guided UI becomes default;
- Advanced Workbench remains available throughout rollout;
- Guided rendering can be disabled without changing project data or issuing commands;
- every layout migration stores a version and is idempotent;
- custom layouts are preserved, not overwritten;
- each session ends at a buildable, tested checkpoint;
- any failed phase can return to Advanced rendering while retaining the shared session/data foundation.

## Definition of Done

The milestone is complete only when:

1. Project is the persistent preparation context in Guided mode.
2. Library preview and project membership are visibly and behaviorally distinct.
3. one Project Plan tree and next action replace duplicate navigation.
4. Direct Carve cannot silently create, replace, or write into the wrong project.
5. Prepare and Run communicate through an immutable handoff.
6. active streaming prevents unsafe project/context switching.
7. Guided and Advanced experiences share one project/session truth.
8. saved layouts migrate without destroying custom presets.
9. all new modules have direct enabled/disabled and routing tests where applicable.
10. every substantially edited production file is at or below 750 lines, with new files targeting 500.
11. the full automated suite and packaging smoke tests pass.
12. novice usability release gates pass before Guided becomes the default.

## Current Execution Action

Engineering Sessions 01–17 and the Session 18 automated, package, and 72-state
responsive/keyboard visual handoff are complete. Conduct the exact five-person
River Sign protocol in `PROJECT-CENTERED-WORKSHOP-USABILITY.md`. Preserve
Advanced Workbench as the default until real raw results satisfy every release
formula; do not infer human acceptance from automated tests or screenshots. See
`PROJECT-CENTERED-WORKSHOP-RELEASE-VALIDATION.md` for the final engineering,
visual, and artifact evidence.
