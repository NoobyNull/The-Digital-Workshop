---
milestone: v0.7.0
phase: 40
session: 01
status: complete
captured: 2026-07-10
head: 5347e226c2bdbe207a183979de8828feca048b7f
---

# Project-Centered Workshop Baseline

This is the preservation and measurement record taken before the v0.7 workshop
architecture changes. It describes the current dirty source work, verified build,
current navigation, oversized-file baseline, and the canonical beginner task.

## Preservation Boundary

The source baseline contains exactly 22 modified tracked paths and six untracked
source/test paths. No source file was changed while creating this record.

An ignored local recovery copy exists at:

- `.planning/baselines/v0.7-session-01/pre-existing-tracked-source.patch`
- `.planning/baselines/v0.7-session-01/pre-existing-untracked-source.tar.gz`

Recovery fingerprints:

| Artifact | SHA-256 |
|---|---|
| tracked source patch | `f8e45f5f5416f82af74ced243f1a373a1a3e37a0b85b8d868a860106bb61ffa3` |
| untracked source archive | `5f14e8fe21eec616c43c379433a0b506c82cef07188cab43282f6b04ee7e1b6b` |

Do not apply either artifact over a later worktree. They are emergency recovery
material; inspect and apply individual paths or hunks deliberately.

### Stream A — sidecar thumbnails

Purpose: discover a nearby model image, fit it into a cached thumbnail, prefer it
during import, and retain generated-thumbnail fallback.

- `src/CMakeLists.txt`
- `src/core/import/thumbnail_sidecar.cpp` (untracked at baseline)
- `src/core/import/thumbnail_sidecar.h` (untracked at baseline)
- `src/core/library/image_thumbnail.cpp` (untracked at baseline)
- `src/core/library/image_thumbnail.h` (untracked at baseline)
- `src/core/library/library_manager.cpp`
- `src/core/library/library_manager.h`
- `src/core/loaders/texture_loader.cpp`
- `src/core/loaders/texture_loader.h`
- `src/managers/file_io_manager.cpp`
- `tests/test_library_manager.cpp`
- `tests/test_thumbnail_sidecar.cpp` (untracked at baseline)
- thumbnail-specific hunks in `tests/CMakeLists.txt`

This stream overlaps the future Library picker/import handoff. Preserve the
sidecar preference and fallback behavior when Library ownership is extracted.

### Stream B — viewport navigation

Purpose: add view rotation controls, D/C/M navigation-style cycling, reliable
recenter/reset behavior, and transformed-model-aware camera targeting.

- `src/core/config/config.cpp`
- `src/core/config/config.h`
- `src/core/viewport/view_cube_orientation.cpp`
- `src/core/viewport/view_cube_orientation.h`
- `src/render/camera.cpp`
- `src/render/camera.h`
- navigation/camera hunks in `src/ui/panels/viewport_panel.cpp`
- `src/ui/panels/viewport_panel.h`
- `tests/test_camera.cpp`
- `tests/test_navigation_style.cpp` (untracked at baseline)
- `tests/test_view_cube_orientation.cpp`
- navigation-specific hunks in `tests/CMakeLists.txt`

This stream overlaps the later config and Viewport decompositions. Its toolbar
controls also need small-width validation after extraction.

### Stream C — Direct Carve preview isolation

Purpose: prevent ordinary model loads from inheriting stale Direct Carve fit
transforms while Direct Carve is hidden.

- `src/app/application_callbacks.cpp`
- `src/ui/panels/direct_carve_panel.cpp`
- `src/ui/panels/direct_carve_panel.h`
- the two `clearFitParams()` model-load hunks in
  `src/ui/panels/viewport_panel.cpp`
- `tests/test_direct_carve_ui_copy.cpp`

The visible-panel test is an interim proxy for preview ownership. v0.7 must
preserve the regression behavior while replacing that proxy with explicit
ProjectSession route/origin state.

### Shared-file warning

`tests/CMakeLists.txt` and `src/ui/panels/viewport_panel.cpp` contain hunks from
multiple streams. Never assign either whole file to one stream or overwrite it
from a saved copy. Inspect hunks before staging or migrating them.

## Verified Baseline

Canonical build and test commands:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDW_BUILD_TESTS=ON
cmake --build build -j$(nproc)
./build/tests/dw_tests
```

Verified on 2026-07-10:

- build type: Release;
- test binary newer than every source, header, test, CMake input, and fetched
  dependency source;
- 1,154 tests in 128 suites passed;
- zero failures;
- GoogleTest time: 956 ms; wall time: 0.961 s;
- `./build/digital_workshop --diagnostic --verbose` initialized successfully
  inside an isolated temporary HOME/XDG profile;
- diagnostic startup total: 286.88 ms.

The clean-profile diagnostic logs three existing duplicate-column errors while
opening `tools.vtdb` (`spindle_power_watts`, `max_rpm`, and `drive_type`). Startup
still succeeds. This is baseline evidence, not part of the workshop UX scope.

## Current Oversized Files

| File | Baseline lines | v0.7 destination |
|---|---:|---|
| `src/ui/panels/direct_carve_panel.cpp` | 3,369 | thin orchestrator plus step modules |
| `src/ui/panels/viewport_panel.cpp` | 1,860 | shell, interaction, toolbar, and layers |
| `src/core/database/project_repository.cpp` | 1,381 | project CRUD and open-item implementation |
| `src/core/library/library_manager.cpp` | 1,092 | extract only touched interaction ownership |
| `src/app/application_wiring.cpp` | 1,081 | composition and focused module wiring |
| `src/core/config/config.cpp` | 1,027 | layout migration extracted |
| `src/ui/panels/library_panel.cpp` | 776 | browser shell and picker view |
| `src/ui/panels/library_panel_items.cpp` | 761 | presentation and action policy |
| `src/ui/panels/project_panel.cpp` | 585 | replaced by Project Plan components |

New and substantially rewritten files target 500 lines. The hard ceiling is
750 lines. A split only counts when state and decisions move behind a named
contract with direct tests.

## Baseline Screens

The images under `.planning/project-centered-workshop-baseline/` were captured
at 1920 x 1080 using a 1600 x 900 application window, a clean isolated HOME/XDG
profile, Xvfb, and Virtual CNC. No personal library or physical controller was
used.

1. [01-home.png](project-centered-workshop-baseline/01-home.png) — Start Page duplicates New/Open while Library and Project are
   already present behind it.
2. [02-library-project.png](project-centered-workshop-baseline/02-library-project.png) — Library and Project occupy equal stacked panels;
   each is an independent starting point and neither explains their relationship.
3. [03-direct-carve-sender-tab.png](project-centered-workshop-baseline/03-direct-carve-sender-tab.png) — opening Direct Carve places it beside the
   Viewport in the CNC Sender workspace, losing the Workshop context.
4. [04-run-sender.png](project-centered-workshop-baseline/04-run-sender.png) — the Sender exposes ten peer tabs plus Tool & Material;
   project identity and next action are absent.

## Current Navigation Contract

- Home is a floating Start Page over an already active Workshop layout.
- New Project creates a hardcoded `New Project`; there is no naming step.
- A Library item single-click selects it globally; double-click loads it globally.
- Add to Project is available only from an item context menu and only after a
  project exists.
- Project shows a Work Order plus separate Models/G-code/material/cost sections,
  duplicating the project representation.
- Direct Carve belongs to Sender and presents nine steps: Model, Tool, Material,
  Preview, Machine, Zero, Outline, Confirm, and Running.
- Run readiness is reached through Direct Carve, but the shell does not retain a
  visible project/item identity during that transition.

## Canonical River Sign Task

Use only the project-authored files in `tests/fixtures/ux/river_sign/`:

- `river_sign_primary.stl`
- `river_sign_alternate.stl`
- `river_sign_preview_only.stl`

Moderated baseline script:

1. Ask: "Create a project called River Sign using Primary."
2. Record the current naming failure without coaching. Then say: "Treat New
   Project as River Sign" so the remaining baseline can continue.
3. Ask: "Add Alternate to the same project."
4. Ask: "Preview Preview Only without adding it, then return to River Sign."
5. Ask the participant which designs belong to the project.
6. Ask: "Continue Primary into carve preparation."
7. Use Virtual CNC and a manually selected 3.175 mm ball-nose tool. Never connect
   a physical machine for this task.
8. Reach the final review, then save, close, reopen, and identify the active
   project and selected design.

After each task ask:

- What project are you in?
- What design is visible?
- Is that design part of the project?
- What would you do next?

Record completion time, success, critical errors, clicks, backtracks, workspace
or tab hunting, assistance, project identification, membership identification,
accidental mutation, and a 1–7 ease score.

## Session 01 Gate

- Existing dirty work: inventoried, fingerprinted, and locally recoverable.
- Full test suite: green.
- Diagnostic initialization: green in isolation.
- Baseline screens: captured.
- File sizes and current navigation: recorded.
- Canonical River Sign fixture and task: recorded.
- Visible application behavior: unchanged.

The next safe bite is Phase 40 Session 02: add `dw_workshop_core`, minimal typed
contracts, dependency/boundary tests, and the edited-file size ratchet.
