# dw-bridge — headless CAM sidecar for Digital Workshop

A localhost JSON/HTTP API (plus a CLI) that wraps the **PureCutCNC** CAM
engine so native applications — e.g. the C++ Digital Workshop app — can go
**model → toolpaths → G-code** with plain HTTP calls. No JavaScript is
embedded in DW; the bridge runs as a separate Node process.

PureCutCNC has no API of its own (confirmed in its `ARCHITECTURE.md` §10) —
its engine is a set of pure TypeScript functions. This bridge is that
engine's missing API surface.

## Isolated environment layout

Everything lives in a self-contained directory — the Node equivalent of a
Python virtualenv. The upstream repo is vendored **pristine** (no installs,
no edits inside it; `git pull` stays clean), and all dependencies are
private to the env:

```
/data/DW/cambridge/            ← the isolated environment
  package.json                 ← env manifest (6 runtime deps + npm scripts)
  node_modules/                ← private deps; nothing global, nothing in the clone
  vendor/purecutcnc/           ← pristine upstream source (engine imports resolve
                                 up-tree to cambridge/node_modules)
  bridge/                      ← this API (spec/builder/runner/server/cli + docs)
```

Updating the engine later: `git -C vendor/purecutcnc pull`, then
`npm run smoke` to confirm the contract still holds.

```
 DW (C++)                      dw-bridge (Node/tsx)              PureCutCNC engine
┌──────────────┐   POST /api/job (JobSpec JSON)  ┌─────────────┐   pure functions   ┌──────────────────┐
│ model library│ ──────────────────────────────▶ │ server.ts   │ ─────────────────▶ │ toolpaths/       │
│ job params   │                                 │ runner.ts   │                    │  pocket/edge/    │
│              │ ◀────────────────────────────── │ builder.ts  │ ◀───────────────── │  vcarve/surface/ │
│ gcode preview│   { gcode, stats, warnings }    │ machines.ts │   ToolpathResult   │  drilling +      │
│ Run CNC      │                                 └─────────────┘                    │ gcode/postproc   │
└──────────────┘                                                               └──────────────────┘
```

## Requirements

- Node.js 20+ (developed on v26)
- Dependencies installed inside the env: `npm install` in `/data/DW/cambridge` (already done)

## Run

```bash
cd /data/DW/cambridge

# HTTP sidecar (default http://127.0.0.1:8973, override with DW_BRIDGE_PORT)
npm run bridge

# CLI (same pipeline, no server)
npm run cli -- bridge/examples/dome-fluidnc.json [--out job.nc] [--camj job.camj]

# End-to-end self test (28 checks: 3D surface + 2.5D pocket/edge/vcarve/drill)
npm run smoke

# Verify a generated .camj against the app's own strict file decoder
npm run verify -- bridge/examples/dome-example.camj

# PureCutCNC web UI (Sketch / 3D View / Simulation) at http://localhost:5173
npm run ui
```

## Visual verification & simulation workflow

The bridge generates; the **PureCutCNC web UI verifies**. Geometry comes
from your models — the UI's hand-drawing tools (rect/circle/polygon
sketching) go unused in this workflow.

```bash
npm run ui          # start the interface (first time: npm install in vendor/purecutcnc)
npm run cli -- job.json            # writes job.nc + job.camj
```

Then in the browser at `http://localhost:5173`:

1. **Open** the generated `.camj` — the project loads with stock, model,
   tools, and operations exactly as the bridge built them
   (`npm run verify` proves app-compatibility headlessly before you bother)
2. **Sketch view** — inspect feature placement and selected toolpaths in 2D
3. **3D View** — check the CSG model, entry/exit moves, path direction
4. **Simulation** — replay the toolpaths against the stock (voxel material
   removal) to confirm pockets, islands, surfaces, and carving behavior
5. Cut the `.nc` the bridge emitted — it is byte-identical in intent to
   what the UI would export (same engine, same postprocessor, same order)

Notes:

- The `.camj` is **self-contained**: when a job targets FluidNC, that
  machine definition is embedded in the file, so the UI's machine selector
  shows it and in-app export works too.
- Simulation is a verification aid, not a safety guarantee — the operator
  contract in PureCutCNC's `PROJECT.md` still applies to every job.
- The UI's dev server needs its own deps: `npm install` inside
  `vendor/purecutcnc` (already done; `node_modules` is git-ignored
  upstream, so the vendor clone stays clean).

## HTTP API

### `GET /api/health`
```json
{ "ok": true, "service": "dw-bridge", "version": 1 }
```

### `GET /api/machines`
Lists postprocessor definitions: `generic`, `grbl`, `grblhal`, `mach3`,
`uccnc`, `linuxcnc`, `fluidnc` (FluidNC added by this bridge, modeled on
GRBL 1.1 + `G54` WCS and M7/M8/M9 coolant — see `machines/fluidnc.json`).

### `POST /api/job`
Body: a **JobSpec** (below). Returns `200` with the full result (G-code
inline) or `422` with `{ "ok": false, "error", "details?" }`.

```json
{
  "ok": true,
  "gcode": "G90\nG17\n...",
  "stats": { "lineCount": 12148, "operationCount": 2, "moveCount": 12127 },
  "warnings": [],
  "operations": [
    { "id": "op1", "kind": "rough_surface", "name": "rough_surface 1", "moveCount": 838, "warnings": [] }
  ],
  "files": { "gcode": "/abs/job.nc", "camj": "/abs/job.camj" }
}
```

## JobSpec

```jsonc
{
  "name": "dome",                    // optional; used for program name
  "units": "mm",                     // "mm" (default) | "inch"
  "machine": "fluidnc",              // required — id from /api/machines
  "stock": "auto",                   // or { "width":100, "height":80, "thickness":12, "material":"oak" }
  "stockMargin": 2,                  // only for stock:"auto" (default 0)
  "tools": [ /* ToolSpec[] */ ],
  "features": [ /* FeatureSpec[] */ ],
  "operations": [ /* OperationSpec[] */ ],
  "options": { "emitToolChanges": true, "emitCoolant": false, "programName": "..." },
  "outputPath": "/abs/job.nc",       // optional — also write G-code to disk
  "saveProjectPath": "/abs/job.camj" // optional — also write the .camj project
}
```

### Z coordinate contract

All Z values are **absolute project Z**: stock bottom = `0`,
stock top = `stock.thickness`. The bridge places the machine origin at
stock front-left-top, so in the emitted G-code **Z0 = stock top surface**
and cuts are negative — the standard probe-the-top GRBL workflow.

### ToolSpec

```jsonc
{ "id": "rough",                      // required, referenced by operations
  "type": "flat_endmill",             // flat_endmill | ball_endmill | v_bit | drill
  "diameter": 6,                      // project units
  "vBitAngle": 60,                    // v_bit only (default 60)
  "flutes": 2, "material": "carbide", // optional
  "rpm": 18000, "feed": 1500, "plungeFeed": 500,
  "stepdown": 2, "stepover": 0.45,    // stepover is a FRACTION of diameter (0..1]
  "maxCutDepth": 0 }                  // 0 = unlimited
```

### FeatureSpec

| type | fields | notes |
|---|---|---|
| `mesh` | `path` (abs, STL or OBJ), `format?`, `scale?`, `axisSwap?` (`none`/`yz`/`xz`/`xy`), `zTop?`, `zBottom?` | One mesh per job. `scale` converts file units (e.g. `25.4` for inch-authored STL in an mm project). Default Z: mesh bottom at 0, top at mesh height; overriding `zTop`/`zBottom` Z-scales the model into the band (min scale 0.1). `stock:"auto"` sizes stock to the mesh silhouette. |
| `rect` | `x, y, w, h`, `zTop`, `zBottom`, `role?` | |
| `circle` | `cx, cy, r`, `zTop`, `zBottom`, `role?` | |
| `polygon` | `points: [[x,y],...]`, `closed?` (default true), `zTop`, `zBottom`, `role?` | `closed:false` = open polyline for `follow_line` engraving |

`role`: `subtract` (default — material to remove) | `add` (material that
remains — bosses, part outlines) | `line` (pure line geometry for
v-carve/follow-line).

### OperationSpec

```jsonc
{ "kind": "pocket",            // see table below
  "target": ["pocket1"],       // feature ids
  "tool": "rough",             // tool id
  // everything else optional — sensible defaults come from the tool:
  "stepdown": 2, "stepover": 0.4, "feed": 800, "plungeFeed": 300, "rpm": 18000,
  "pocketPattern": "offset",   // offset | parallel | waterline
  "pocketAngle": 0,            // raster angle for parallel pattern
  "stockToLeaveRadial": 0, "stockToLeaveAxial": 0,
  "cutDirection": "conventional", "machiningOrder": "level_first",
  "drillType": "peck", "peckDepth": 2, "dwellTime": 0.5, // drilling only
  "maxCarveDepth": 4,                                   // v-carve only
  "waterlineAdaptiveRefinement": true /* ... */         // finish_surface waterline
}
```

| kind | accepts targets of role | tool |
|---|---|---|
| `pocket` | `subtract` (cavities) + `add` in target = islands | any mill |
| `edge_route_inside` / `edge_route_outside` | `add`, `model`, `region` — **not** `subtract` | any mill |
| `v_carve` / `v_carve_medial` | `subtract` regions, `line` (closed or open) | `v_bit` |
| `follow_line` | open `line` polylines | any mill |
| `drilling` | `subtract` circles/points | `drill` |
| `rough_surface` | the `mesh` feature | flat endmill typical |
| `finish_surface` | the `mesh` feature | ball endmill typical; `pocketPattern: parallel` (uses `pocketAngle`) or `waterline` |
| `finish_surface_cleanup` | the `mesh` feature | after waterline finish |
| `surface_clean` | `add` bosses/pads | any mill |

Operations execute in array order. The job **fails fast** if any operation
produces zero moves — partial G-code is never emitted.

## Safety

- The bridge prepends a **safe program start** (`G0 Z<safe>` then rapid to
  the entry point) — upstream toolpaths mark the entry with a zero-length
  rapid that the postprocessor drops, which would otherwise make the first
  motion a diagonal plunge from the machine's parking position.
- This is still a generator, not a guarantee. Verify every job in a
  preview/simulation (e.g. DW's own G-code view) and follow PureCutCNC's
  `PROJECT.md` safety contract: the operator owns machine, stock,
  workholding, tooling, feeds/speeds, and the physical test procedure.

## DW integration notes

DW calls the bridge with any HTTP client (libcurl etc.):

1. DW resolves a library model → absolute STL/OBJ path.
2. DW builds a JobSpec from its project/material/tool data and POSTs it.
3. DW feeds `result.gcode` into its **existing** G-code parser, toolpath
   preview, and Run CNC pipeline — no new G-code handling needed.
4. `result.operations[].warnings` are structured `{code, params}` from the
   engine (e.g. `stepoverRatioRange`, `noToolAssigned`) — stable codes DW
   can map to its own UI strings.

Start the sidecar as a child process (`npm run bridge` with
cwd = `/data/DW/cambridge`) or as a user service. It binds to
`127.0.0.1` only.

## Current limits

- One mesh feature per job (primitives are unlimited).
- Tabs/clamps are applied by the engine but not yet exposed in JobSpec.
- SVG/DXF import is not wired (upstream importers target the browser UI);
  use `polygon` features or pre-convert to STL.
- `.camj` output (format 3.0) can be opened in the PureCutCNC app for
  visual verification of the exact project the bridge built.

## Files

```
bridge/
  spec.ts            JobSpec/result model — the API contract
  builder.ts         JobSpec -> PureCutCNC Project (format 3.0)
  runner.ts          toolpath orchestration + safe-start anchor + posting
  machines.ts        postprocessor registry (bundled + FluidNC)
  machines/fluidnc.json
  server.ts          node:http JSON API (127.0.0.1:8973)
  cli.ts             npm run cli -- job.json [--out] [--camj]
  examples/          ready-to-run job specs
  test/smoke.ts      end-to-end checks (dome STL gen + all op classes)
```
