# Project-Centered Workshop Release Validation

Validated: 2026-07-11 PDT<br>
Candidate: v0.7.0 (`5347e22-dirty`)<br>
Engineering status: **PASS**<br>
Responsive visual/keyboard status: **PASS — 72/72**<br>
Guided-default status: **HELD — five-person human acceptance pending**

The Project-Centered Workshop implementation, modular boundaries, canonical
journey, safety behavior, responsive UI, source policy, and Linux package paths
pass. Advanced Workbench remains the built-in default because the five-person
novice study is a real-human requirement and has not been conducted.

## Final status

| Gate | Result |
|---|---|
| Requirements | 42/43 complete; only VALX-05 pending |
| Definition of Done | 11/12 proven; human acceptance pending |
| Focused Design Library executable | 38/38 tests, 3 suites |
| Full test executable | 1,587/1,587 tests, 180 suites |
| Sequential CTest | 1,812/1,812 cases, 6.41 seconds |
| Source-size policy | PASS, 524 files, 14 target warnings |
| Responsive matrix | PASS, 72/72 original captures |
| TGZ and `.run` package smoke | PASS, all install/runtime/payload checks |
| Working-tree whitespace | `git diff --check` PASS |
| Human novice study | NOT RUN; release default held |

The detailed requirement and Definition of Done trace is
[`project-centered-workshop-release/COMPLETION-AUDIT.md`](project-centered-workshop-release/COMPLETION-AUDIT.md).

## Automated journey and safety

The canonical regression is:

```text
ProjectCenteredWorkshopEndToEndTest.
CanonicalStartAddPreviewPlanSaveAndRunStartPersistAcrossHomeAndReopen
```

It uses the authored River Sign fixtures and proves named project creation,
explicit Preview Only isolation and exact return, durable/idempotent add,
cross-project rejection, exact operation pinning, preparation through Review &
Run, save, close, fresh-session reopen, and restored project/item identity.

The isolated module suites additionally prove:

- ProjectSession generation, dirty-transition, run-lock, and stale-completion
  behavior;
- purpose-bound Library selection, import, preview, add, cancel, and restore;
- deterministic Project Plan hierarchy, blockers, readiness, and one next
  action;
- editable preparation that cannot express machine effects;
- immutable RunPackage identity/fingerprint and exact lock-before-stream order;
- pause, resume, abort, emergency/failure priority, monotonic progress, and
  terminal cleanup;
- versioned/idempotent layout migration and custom-preset preservation;
- shared Guided/Advanced commands and inert disabled-Guided behavior.

Final logs are under `project-centered-workshop-release/`:

- `final-design-library-tests.log`
- `final-dw-tests.log`
- `final-ctest.log`
- `final-source-sizes.log`
- `final-ux-matrix-validation.log`
- `final-human-study-runner-tests.log`
- `final-human-study-scorer-tests.log`
- `final-human-study-blank.log`

## Responsive and keyboard matrix

The compile-gated real application was captured in 12 deterministic states for
each required environment:

| Set | Work area | UI scale | Result |
|---|---:|---:|---|
| R1 | 1366 x 768 | 100% | PASS 12/12 |
| R2 | 1366 x 768 | 150% | PASS 12/12 |
| R3 | 1366 x 768 | 200% | PASS 12/12 |
| R4 | 3840 x 2160 | 100% | PASS 12/12 |
| R5 | 3840 x 2160 | 150% | PASS 12/12 |
| R6 | 3840 x 2160 | 200% | PASS 12/12 |

Each capture used an isolated Xvfb display, fresh HOME/XDG profile, software
OpenGL, Virtual CNC, and the requested scale. The compile-gated application
wrote its completed OpenGL back buffer before announcing readiness, avoiding
asynchronous window-grab races. The validator reports:

```text
PASS: 72 captures; 6 environments x 12 states; build 5347e22-dirty; version 0.7.0
```

Manifest:
`project-centered-workshop-release/ux-matrix/manifest.json`<br>
Timestamp: `2026-07-11 09:30:33.967363108 -0700`<br>
SHA-256:
`e5d0b4b3481aa4df9719e71cfb7913dce9860570ddb5a5e97d9615c872c3f98b`

Three independent read-only reviews checked all original image hashes and all
72 cells. Project/preview identity, next actions, Back/Cancel, safety controls,
keyboard focus, text/icon status without color, compact-height navigation, and
bounded 4K task surfaces pass. The exact cell record and closed R2-03 label
defect are in
[`project-centered-workshop-release/UX-MATRIX-AUDIT.md`](project-centered-workshop-release/UX-MATRIX-AUDIT.md).

## Packaging

| Artifact | Exact size | SHA-256 |
|---|---:|---|
| `build/DigitalWorkshop-0.7.0-Linux.tar.gz` | 58,219,132 bytes | `ea8f9fae3e973d5e95604dc8b315f2902c5136d42a0b3a3322b1a781ebe159f6` |
| `build/DigitalWorkshop-0.7.0-linux.run` | 58,067,194 bytes | `ea023aaedc90aa438e55fcb2d62ff5edc65ad5ff0a0c431b5e19244350ea7672` |

The final `package-smoke/summary.txt` passes all twelve assertions:

- TGZ application startup and Settings startup;
- `.run` install, application startup, Settings startup, and uninstall;
- GraphQLite payload/load;
- fresh schema initialization;
- desktop entry;
- bundled materials;
- independent River Sign facilitator preflight from the extracted TGZ binary;
- independent River Sign facilitator preflight from the `.run`-installed binary.

Both installed application paths identify as Digital Workshop 0.7.0 with build
`5347e22-dirty`. The smoke uses temporary profiles and leaves no user install
behind.

## Source health

- 524 production files checked.
- Fourteen files are above the 500-line target but within their current policy.
- No milestone-created or substantially rewritten production file exceeds the
  750-line ceiling.
- Four touched legacy files remain above 750 but were not substantially
  rewritten and stay ratcheted: `gcode_panel.cpp` 1,347 lines (1+/1-),
  `library_manager.cpp` 1,092 (31+/2-), `cnc_safety_panel.cpp` 1,012 (1+/1-),
  and `schema.cpp` 822 (4+/13-).
- ProjectSession, Design Library policy, Project Plan, Prepare, Run, viewport
  presentation, and their direct tests have independent build boundaries.
- `git diff --check` passes after the final source and documentation updates.

## Human gate

The exact P01-P05 no-coaching protocol, raw data schema, formulas, and scorer
are in `PROJECT-CENTERED-WORKSHOP-USABILITY.md` and
`project-centered-workshop-release/`.

- Facilitator runner tests: 9/9 PASS.
- Scorer unit tests: 15/15 PASS.
- TGZ and `.run` packaged facilitator preflights: PASS independently.
- Blank study sheet: correctly rejected as `NOT EVALUABLE` / `NOT PASSING`,
  exit 2.
- Actual qualifying participants: 0/5 completed.

No task-success, comprehension, time, backtracking, or SEQ result is inferred
from automation or screenshots.

## Release decision

The scoped refactor and every in-repository engineering/visual gate are
finished. Do **not** change the built-in default to Guided Workshop yet. Run the
five real novice sessions and score their raw observations. Only a passing
cohort may close VALX-05, Phase 48, Session 18, and the v0.7.0 milestone.
