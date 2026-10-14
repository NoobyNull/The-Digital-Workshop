# Workshop Scope — Library, CAM, Sender

- **Date:** 2026-10-14
- **Status:** Accepted direction; the trim it describes landed in this change
- **Supersedes:** the Guided Workshop and Project Plan direction of v0.7.0, and
  the UI-parity requirement and Phases 3–6 of `CAM-INTEGRATION-DESIGN.md`

## What Digital Workshop is

Three things, and nothing else:

1. **Library management**: import, organize, search, and preview the designs
   (STL, OBJ, 3MF) a woodworker owns.
2. **CAM**: turn a design on a piece of stock into G-code.
3. **Sender and machine control**: stream that G-code to a GRBL-family
   machine safely, and drive the machine by hand.

The test for any new feature: does it make one of those three better? Business
tooling (estimates, invoicing, pricing), sheet-goods optimization, and
experience modes do not, and were removed.

## Projects work like slicer projects

A project is what a 3D-printing slicer project file is (OrcaSlicer, Bambu
Studio, PrusaSlicer). The slicer analogy is the design rule: if a slicer
project would not contain it, a Digital Workshop project should not either.

| Slicer | Digital Workshop |
|---|---|
| Project file (`.3mf`) | Project (`.dwproj`) |
| Objects on the plate | Designs placed on the stock |
| Plate / build bed | Stock, origin, clamps |
| Several plates | Several CAM setups (flip jobs, multi-board work) |
| Printer profile | Machine profile |
| Filament profile | Material |
| Process settings | Tools and operations |
| Slice | Generate toolpaths, export G-code |
| "Needs re-slice" | G-code is **Out of date** |
| Send to printer | Sender, through the protected Run handoff |
| Printables / MakerWorld | Design Library |

### What a project contains

The Project panel shows exactly this tree:

```
Design            (project item: model)
└── CAM Setup     (project item: operation)
    └── G-code    (project item: gcode, fingerprinted)
        └── Run   (project item: job)
```

- A **design** is a library model added to the project.
- A **CAM setup** is one stock with its placements, tools, and operations.
  A design can have more than one (top and bottom faces of a flip job).
- **G-code** is what a setup produced. It records the file hash it was
  generated as; when the setup or the design changes afterwards, the item
  reads **Out of date** (the existing `Stale` status) — the slicer's
  "re-slice needed".
- A **run** is one stream of that G-code to the machine. A run cut short
  reads **Interrupted**.

Materials and tool requirements also appear in the tree because the project
index already derives them; they carry no costing data.

### Rules that follow from the slicer model

- **Placement belongs to the setup, not the library model.** Today the
  orientation is stored on the library model (`models.orient_matrix`), so
  rotating a model affects every project that uses it. Once setups own
  placement, the library orientation becomes only the default for a new
  setup.
- **`.dwproj` is the whole project.** It already carries models and G-code;
  it must also carry each setup document so an archive opens on another
  machine exactly as it was left, like a `.3mf`.
- **Presets chain machine → material → operation**, with built-in and user
  presets and a visible "modified" marker, the way a slicer chains printer →
  filament → process.

## PureCutCNC: the math and the CAM process only

PureCutCNC is used as a toolpath engine, not as an application to mirror.

The bridge already exposes the part that matters: a stateless
`POST /api/job` that takes a **JobSpec** — units, stock, tools, features
(mesh, rectangle, circle, polygon, line), and an ordered list of operations —
and returns post-processed G-code with per-operation stats and structured
warnings. Supported operations: `pocket`, `edge_route_inside`,
`edge_route_outside`, `v_carve`, `v_carve_medial`, `follow_line`,
`drilling`, `rough_surface`, `finish_surface`, `finish_surface_cleanup`,
`surface_clean`.

Digital Workshop already parses, previews, simulates, and streams G-code, so
the engine's job ends when it returns G-code. That means:

- **The setup document is Digital Workshop's**, stored in the project
  directory (`setups/<id>.json`), and shaped as a JobSpec so it can be sent
  to the bridge unchanged. CAMJ stays an engine detail.
- **No UI parity requirement.** The native CAM workspace exposes the
  operations woodworkers use first — outside/inside profile, pocket, V-carve,
  drilling, 3D rough and finish — and grows from there. Parameters come from
  Digital Workshop's tool database and presets; the engine's defaults fill
  the rest.
- **Preview is Digital Workshop's own G-code preview.** No move-stream
  endpoint is needed.
- **The bridge stays stateless and small.** Additions are justified by a
  workspace feature (for example tabs and clamps, which the engine applies
  but JobSpec does not yet expose).

### Revised CAM phases

Phases 1 (yank) and 2 (sidecar) are complete. The rest of
`CAM-INTEGRATION-DESIGN.md` is replaced by:

3. **Setup document and persistence.** Setup = JobSpec-shaped JSON stored as
   a project item in the project directory; `.dwproj` carries it. Tool and
   machine records map from the `.vtdb` tool database and machine profiles.
4. **Generate.** "Generate" sends the setup to `POST /api/job`, writes the
   G-code into the project as a child of the setup with its fingerprint, and
   marks earlier G-code from that setup Out of date.
5. **CAM workspace UI.** Stock and placement, the operation list with the
   first operation set above, and tool pickers backed by the existing tool
   library. Preview through the existing viewport toolpath layer.
6. **Run handoff.** Wire the protected Run package (already keyed on the
   setup pin plus the G-code fingerprint) to the Sender, so a project's
   G-code runs through it. Today the Sender streams files directly and the
   protected Run adapter is built but not dispatched.
7. **Validation.** Golden setup → G-code fixtures through the real sidecar,
   packaging smoke on all platforms.

## Removed in this change

| Removed | Notes |
|---|---|
| Project costing (estimates, rate categories, board-foot math) | Existing database tables are left untouched; new databases do not create them |
| Cut Optimizer and per-material stock sizes | Same; `.dwproj` stops writing `costs.json`/`cut_plans.json` and still imports archives that have them |
| GraphQLite graph layer | Was written but never read |
| Six-stage Project Plan | Replaced by the contents tree above |
| Guided carve step flow (`PrepareCarveFlow`, step guidance) | Unused by the application; the setup identity it fed is kept for Run |
| Guided Workshop experience and its layout | Saved layouts migrate to the single Workshop layout (layout migration v2) |
| `DW_BUILD_TOOLS` | Pointed at directories that do not exist |

Kept on purpose: the Materials list (feeds and speeds read it), AI tagging
(a library feature), the protected Run modules, Home, and the Design Library
picker.

## Open decisions

- **AI tagging** (LM Studio / Ollama): keep as a library feature, or remove.
- **Materials**: the app has two material systems — the Materials panel
  (`.dwmat` archives) and the `.vtdb` tool database's own material table that
  holds cutting data. They should become one.
- **Home and the Design Library picker**: simplify toward a slicer start
  screen and a plain "Add from Library".
- **`web/season-countdown/`**: an unrelated app living in this repository.
