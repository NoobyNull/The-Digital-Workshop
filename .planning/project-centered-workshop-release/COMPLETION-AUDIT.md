# Project-Centered Workshop Completion Audit

Audited: 2026-07-11 PDT<br>
Milestone: v0.7.0, Phases 40-48, Sessions 01-18<br>
Engineering result: **PASS**<br>
Requirements: **42 complete / 43 total**<br>
Release result: **HELD — VALX-05 human acceptance is not run**

This is a requirement-by-requirement audit of the current worktree. A checkmark
in a planning file was not accepted as proof by itself; the audit uses current
module boundaries, direct tests, the canonical integration test, original
rendered captures, package execution, and release logs.

## Authoritative final gates

- Focused Design Library executable: 38/38 tests in 3 suites.
- Full `dw_tests`: 1,587/1,587 tests in 180 suites.
- Sequential CTest: 1,812/1,812 cases.
- Canonical E2E:
  `ProjectCenteredWorkshopEndToEndTest.CanonicalStartAddPreviewPlanSaveAndRunStartPersistAcrossHomeAndReopen`.
- Source-size policy: 524 production files checked; PASS with 14 target warnings
  and no new hard-ceiling violation.
- UX matrix: 72/72 original captures pass across six environments; manifest
  SHA-256 `e5d0b4b3481aa4df9719e71cfb7913dce9860570ddb5a5e97d9615c872c3f98b`.
- TGZ smoke: startup, Settings startup, GraphQLite, fresh schema, desktop
  payload, bundled materials, and facilitator preflight pass.
- `.run` smoke: install, startup, Settings startup, uninstall, GraphQLite,
  fresh schema, desktop entry, bundled materials, and facilitator preflight
  pass.
- `git diff --check`: PASS.

Evidence files are `final-design-library-tests.log`, `final-dw-tests.log`,
`final-ctest.log`, `final-source-sizes.log`,
`final-ux-matrix-validation.log`, `package-smoke/summary.txt`,
`UX-MATRIX-AUDIT.md`, `final-human-study-runner-tests.log`,
`final-human-study-scorer-tests.log`, and `final-human-study-blank.log` in this
directory.

## Requirement evidence

| ID | Status | Current-state evidence |
|---|---|---|
| PCW-01 | Complete | `src/modules/project_session/`; session, boundary, integration, and workshop-controller tests prove one typed owner and generation guards. |
| PCW-02 | Complete | Persistent shell and `project_plan_view.cpp`; project-plan presentation/UI and guided-accessibility tests; all 72 captures retain project/preview/stage/action identity. |
| PCW-03 | Complete | `start_page.cpp`, Home flow, and `ProjectLifecycleArchitecture.HomeIsTheOnlyPanelProjectEntrySurface`; global menu commands remain shortcuts, not a second panel. |
| PCW-04 | Complete | `project_item_load_guard`, ProjectSession integration, and lifecycle architecture tests reject stale loads and deterministically clear/restore focus. |
| PCW-05 | Complete | `project_resume`, `project_resume_file_store`, project storage, and fresh-session integration tests restore the valid project and item. |
| LIBX-01 | Complete | `LibraryPickerFlow` has typed Manage, StartProject, and AddToProject purposes with direct flow/presentation tests. |
| LIBX-02 | Complete | Preview is a typed overlay; flow, workshop-controller, and viewport-presentation tests prove it neither adds membership nor replaces project selection. |
| LIBX-03 | Complete | Cancel/Back token and exact-return tests restore the prior selection; the visible action passes all capture environments. |
| LIBX-04 | Complete | Purpose-specific Library actions are ordinary visible buttons; presentation, architecture, and 72-state evidence include Start/Add/Back. |
| LIBX-05 | Complete | Flow tests prove unique missing-only/idempotent adds; presentation and captures name `River Sign` in the action. |
| LIBX-06 | Complete | Picker import completions preserve purpose and offer imported assets without implicit preview or membership; direct flow/coordinator tests pass. |
| PLANX-01 | Complete | `src/modules/project_plan/` and `project_plan_view.cpp` provide the single hierarchy; legacy duplicate section ownership is excluded by architecture tests. |
| PLANX-02 | Complete | Deterministic row roles/actions and UI architecture tests prove every visible row is actionable or informational. |
| PLANX-03 | Complete | `ProjectPlanBuilder` is deterministic and persistence-free; builder, boundary, presentation, input, and run-truth adapter tests pass. |
| PLANX-04 | Complete | Builder/tests and captures expose the six required stages in the required language. |
| PLANX-05 | Complete | Builder and preparation-navigation tests enforce Material & Blank before Choose Tool. |
| RUNX-01 | Complete | `PreparationIdentity`, adapters, and Direct Carve architecture tests pin project, model, operation, and revisions. |
| RUNX-02 | Complete | Application/lifecycle architecture and stale-context tests prohibit hidden project creation, activation, replacement, or wrong-project output. |
| RUNX-03 | Complete | Preparation boundary/application tests require an explicit create-project intent when no project is active. |
| RUNX-04 | Complete | `PrepareCarveFlow` emits preparation state only; `RunPackage` is immutable and boundary tests forbid stream/machine effects. |
| RUNX-05 | Complete | `RunCoordinator` consumes the exact package, owns typed effects and the run lock, and cannot mutate setup identity. |
| RUNX-06 | Complete | Session/run tests reject context switches during streaming and enforce emergency-stop/abort/fail/pause/resume priority. |
| RUNX-07 | Complete | Disabled-coordinator tests prove preview/export remain available and no machine or lock effect is emitted. |
| MODX-01 | Complete | Render-independent `dw_project_session` target, typed results, direct isolated tests, and dependency-boundary tests. |
| MODX-02 | Complete | Render-independent `dw_design_library` target with flow/presentation contracts and 38 focused tests. |
| MODX-03 | Complete | Persistence-free `dw_project_plan` target with direct builder/boundary tests. |
| MODX-04 | Complete | `ProjectWorkshopController` routes typed intents; direct tests and boundary assertions keep policy in domain modules. |
| MODX-05 | Complete | Separate `dw_carve_preparation` and `dw_run_coordination` targets; only immutable `RunPackage` crosses the boundary. |
| MODX-06 | Complete | Workshop boundary and application-architecture tests reject panel-to-panel business dependencies. |
| MODX-07 | Complete | Experience router/layout tests prove Guided and Advanced share ProjectSession/repositories; disabled Guided is inert. |
| MODX-08 | Complete | Focused render-independent targets are linked by both app and isolated test executables. |
| MODX-09 | Complete | Source policy passes 524 files; new/owned units stay below 750 and target roughly 500; minimally touched legacy monoliths remain ratcheted. |
| MODX-10 | Complete | Direct Carve, viewport, application wiring, repository/config/database, and Library behavior are split into owned modules/files with boundary tests. |
| MIGX-01 | Complete | Advanced remains the built-in default; both experience modes share commands and truth. No human acceptance is claimed. |
| MIGX-02 | Complete | `layout_migration` and its direct tests prove versioned, idempotent built-in migration. |
| MIGX-03 | Complete | Migration tests preserve custom presets byte-for-byte/semantically unchanged. |
| MIGX-04 | Complete | Compatibility/E2E/database/package tests cover existing projects, Library records, imported G-code, and external `.nc` paths. |
| MIGX-05 | Complete | Material copy and plan/preparation contracts distinguish Library metadata from project operation intent. |
| VALX-01 | Complete | The full and isolated suites cover lifecycle, preview isolation, add idempotence, plan derivation, pinning, locks, migration, and persistence. |
| VALX-02 | Complete | Session evidence plus final targeted/full/CTest/diff/line-count gates pass; current final logs are listed above. |
| VALX-03 | Complete | The compile-gated real app passes 72/72 states at 1366x768 and 4K, each at 100%, 150%, and 200%. |
| VALX-04 | Complete | Architecture tests plus original-image review prove navigable controls, visible keyboard focus, and text/icon meaning independent of color. |
| VALX-05 | Pending | P01-P05 are deliberately `NOT RUN`. The blank sheet fails closed with exit 2; 9/9 runner tests, 15/15 scorer tests, and both packaged preflights pass. |

## Definition of Done audit

| # | Definition of Done item | Result |
|---:|---|---|
| 1 | Project is the persistent Guided preparation context | PASS |
| 2 | Library preview and project membership are visibly and behaviorally distinct | PASS |
| 3 | One Project Plan and one next action replace duplicate navigation | PASS |
| 4 | Direct Carve cannot silently create, replace, or write into the wrong project | PASS |
| 5 | Prepare and Run communicate through an immutable handoff | PASS |
| 6 | Active streaming blocks unsafe project/context switching | PASS |
| 7 | Guided and Advanced share one session truth | PASS |
| 8 | Built-in layouts migrate without destroying custom presets | PASS |
| 9 | New modules have direct enabled/disabled and routing tests where applicable | PASS |
| 10 | Substantially edited production files remain at or below 750; new files target 500 | PASS |
| 11 | Full automation and both package smoke paths pass | PASS |
| 12 | Novice usability gates pass before Guided becomes default | PENDING — correctly enforced by Advanced remaining default |

## Release decision

Sessions 10-17 and every Session 18 engineering, package, and responsive visual
gate are complete. The requested architecture and UX refactor are implemented
and verified. The milestone itself is not release-complete because five actual
inexperienced hobbyists have not yet performed the no-coaching River Sign
study. No automated or simulated evidence can close that requirement.
