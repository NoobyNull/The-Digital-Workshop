# Project-Centered Workshop UX Matrix Audit

Audited: 2026-07-11 PDT  
Candidate: Digital Workshop v0.7.0 (`5347e22-dirty`)  
Result: **PASS — 72/72 capture cells**

## Authoritative capture set

- Directory: `ux-matrix/`
- Manifest: `ux-matrix/manifest.json`
- Manifest timestamp: `2026-07-11 09:30:33.967363108 -0700`
- Manifest SHA-256:
  `e5d0b4b3481aa4df9719e71cfb7913dce9860570ddb5a5e97d9615c872c3f98b`
- Images: 72 uncropped PNG files, 6 environments by 12 deterministic states
- Runtime: compile-gated capture binary, isolated Xvfb display, fresh HOME/XDG
  profile, software OpenGL, Virtual CNC, and application-owned back-buffer PNG
  output before the ready handshake

The machine validator was run against the directory after capture:

```text
$ python3 scripts/validate_ux_matrix.py \
    .planning/project-centered-workshop-release/ux-matrix
PASS: 72 captures; 6 environments x 12 states; build 5347e22-dirty; version 0.7.0
```

The validator confirms the exact file set, file hashes, requested and actual
dimensions, scale, build/version identity, scenario order, route, project,
selected design, and focus metadata. The independent image audit covers actual
pixel content. `git diff --check` also passes.

## State legend

| State | Surface |
|---:|---|
| 01 | Guided Home |
| 02 | Library — Start Project |
| 03 | Library — explicit Preview Only |
| 04 | Project Plan |
| 05 | Prepare — Design & Size |
| 06 | Prepare — Material & Blank |
| 07 | Prepare — Choose Tool |
| 08 | Prepare — Carve Preview |
| 09 | Review & Run — missing requirement |
| 10 | Review & Run — ready |
| 11 | Virtual Run — streaming |
| 12 | Virtual Run — paused, Hold to Abort keyboard-focused |

## Cell results

| Set | Work area | Scale | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 |
|---|---:|---:|---|---|---|---|---|---|---|---|---|---|---|---|
| R1 | 1366 x 768 | 100% | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| R2 | 1366 x 768 | 150% | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| R3 | 1366 x 768 | 200% | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| R4 | 3840 x 2160 | 100% | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| R5 | 3840 x 2160 | 150% | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| R6 | 3840 x 2160 | 200% | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

Three independent read-only reviews split the current manifest into R1/R4,
R2/R5, and R3/R6. Each reviewer verified the manifest identity and original PNG
hashes before inspecting all 24 assigned images. Every environment passed
12/12 with no defect.

## Acceptance checklist

| Check applied to every relevant image | Result |
|---|---|
| Project and design or preview identity are visible and unambiguous | PASS |
| Primary and Back/Cancel actions are completely visible | PASS |
| Labels, statuses, inputs, dialogs, and tooltips do not overlap or clip | PASS |
| No horizontal scroll is needed to reach a primary action | PASS |
| Modal controls remain inside the requested work area | PASS |
| `Hold to Abort` has a visible keyboard-focus outline in state 12 | PASS |
| Complete, locked, warning, missing, ready, streaming, and paused states use text/icons as well as color | PASS |
| Library preview and project membership are separately readable | PASS |
| Advanced details remain secondary to the novice's next action | PASS |
| 4K task content stays bounded and connected to its actions | PASS |
| 1366 x 768 retains Continue/Back and run safety controls | PASS |

## Defect caught and closed

The pre-final R2 state-03 review found the second line of `Preview Only`
touching the child-window clip boundary at 1366 x 768 / 150%. The Library card
now reserves wrapped label height and the card list adds a real bottom scroll
clearance, allowing selected-last-item alignment to be honored. Five targeted
R2-03 reruns were pixel-identical before the entire 72-state matrix was rebuilt.

The current R2-03 original was checked at native pixels: `Preview` and `Only`
are complete, the lowercase `y` descender is intact, and visible card-background
padding remains below the glyphs. Only the manifest and images listed above are
release evidence; the superseded failing matrix is not.

The final evidence path also no longer depends on an asynchronous X11 window
grab. The compile-gated application writes the completed OpenGL back buffer to
PNG before its ready handshake, then holds that rendered frame while the
harness verifies dimensions and records the hash. A direct architecture
regression guards that ordering. Final reviewers inspected one PNG per image
call; all 72 files were fully opaque and matched their manifest hashes.

## Scope boundary

This audit proves rendering, responsive fit, explicit state copy, and the
captured keyboard-focus/color-independence presentation. It does not prove that
an inexperienced person understands the flow or completes it without coaching.
The P01-P05 River Sign study remains `NOT RUN`, so Advanced Workbench remains
the built-in default.
