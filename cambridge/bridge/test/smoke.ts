/**
 * dw-bridge — end-to-end smoke test (no HTTP).
 *
 * Generates a watertight "dome on a base" binary STL, then runs:
 *   job 1: rough_surface + finish_surface over the mesh (fluidnc post)
 *   job 2: pocket + edge_route_outside + v_carve + drilling (grbl post)
 * asserting on G-code structure and Z sanity.
 *
 *   npx tsx dw-bridge/test/smoke.ts
 */

import { mkdirSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { runJob } from '../runner'
import type { JobSpec } from '../spec'

const here = dirname(fileURLToPath(import.meta.url))
const outDir = join(here, 'out')
mkdirSync(outDir, { recursive: true })

// ---------------------------------------------------------------- STL gen

type V3 = [number, number, number]

function normal(a: V3, b: V3, c: V3): V3 {
  const u: V3 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]]
  const v: V3 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]]
  const n: V3 = [
    u[1] * v[2] - u[2] * v[1],
    u[2] * v[0] - u[0] * v[2],
    u[0] * v[1] - u[1] * v[0],
  ]
  const len = Math.hypot(n[0], n[1], n[2]) || 1
  return [n[0] / len, n[1] / len, n[2] / len]
}

/** Watertight block (w x d x baseH) with a smooth dome rising to totalH. */
function domeStl(w: number, d: number, baseH: number, totalH: number, nx = 24, ny = 16): Buffer {
  const tris: Array<[V3, V3, V3]> = []
  const domeH = totalH - baseH
  const cx = w / 2
  const cy = d / 2
  const rMax = Math.min(w, d) / 2

  const zAt = (x: number, y: number): number => {
    const r = Math.hypot(x - cx, y - cy)
    const t = Math.max(0, 1 - (r / rMax) ** 2)
    return baseH + domeH * t
  }

  // Top surface grid (two triangles per cell)
  for (let iy = 0; iy < ny; iy++) {
    for (let ix = 0; ix < nx; ix++) {
      const x0 = (ix / nx) * w
      const x1 = ((ix + 1) / nx) * w
      const y0 = (iy / ny) * d
      const y1 = ((iy + 1) / ny) * d
      const p00: V3 = [x0, y0, zAt(x0, y0)]
      const p10: V3 = [x1, y0, zAt(x1, y0)]
      const p01: V3 = [x0, y1, zAt(x0, y1)]
      const p11: V3 = [x1, y1, zAt(x1, y1)]
      tris.push([p00, p11, p10], [p00, p01, p11])
    }
  }
  // Bottom
  tris.push(
    [[0, 0, 0], [w, 0, 0], [w, d, 0]],
    [[0, 0, 0], [w, d, 0], [0, d, 0]],
  )
  // Side walls — top edge follows the surface height along each boundary.
  const edge = (a: V3, b: V3): void => {
    const at: V3 = [a[0], a[1], zAt(a[0], a[1])]
    const bt: V3 = [b[0], b[1], zAt(b[0], b[1])]
    tris.push(
      [[a[0], a[1], 0], [b[0], b[1], 0], bt],
      [[a[0], a[1], 0], bt, at],
    )
  }
  for (let ix = 0; ix < nx; ix++) {
    const x0 = (ix / nx) * w
    const x1 = ((ix + 1) / nx) * w
    edge([x0, 0, 0], [x1, 0, 0])
    edge([x1, d, 0], [x0, d, 0])
  }
  for (let iy = 0; iy < ny; iy++) {
    const y0 = (iy / ny) * d
    const y1 = ((iy + 1) / ny) * d
    edge([0, y1, 0], [0, y0, 0])
    edge([w, y0, 0], [w, y1, 0])
  }

  const buf = Buffer.alloc(84 + tris.length * 50)
  buf.writeUInt32LE(tris.length, 80)
  tris.forEach(([a, b, c], i) => {
    const n = normal(a, b, c)
    const o = 84 + i * 50
    for (let k = 0; k < 3; k++) buf.writeFloatLE(n[k], o + k * 4)
    for (const [j, p] of [a, b, c].entries()) {
      for (let k = 0; k < 3; k++) buf.writeFloatLE(p[k], o + 12 + j * 12 + k * 4)
    }
    buf.writeUInt16LE(0, o + 48)
  })
  return buf
}

// ---------------------------------------------------------------- checks

let failures = 0

function check(name: string, cond: boolean, detail = ''): void {
  if (cond) {
    console.log(`  ok  ${name}`)
  } else {
    failures += 1
    console.log(`FAIL  ${name} ${detail}`)
  }
}

function checkGcode(label: string, gcode: string, stockThickness: number): void {
  const lines = gcode.split('\n')
  check(`${label}: non-empty`, lines.length > 20, `lines=${lines.length}`)
  check(`${label}: units cmd`, /G21/.test(gcode))
  check(`${label}: spindle on`, /M3/.test(gcode))
  check(`${label}: program end`, /M30/.test(gcode))
  check(`${label}: has linear moves`, /G1 X/.test(gcode))

  const zValues = [...gcode.matchAll(/Z(-?\d+(?:\.\d+)?)/g)].map((m) => Number(m[1]))
  const minZ = Math.min(...zValues)
  const maxZ = Math.max(...zValues)
  check(`${label}: Z below stock top`, minZ < 0, `minZ=${minZ}`)
  check(
    `${label}: Z not absurdly deep`,
    minZ >= -(stockThickness + 1),
    `minZ=${minZ} thickness=${stockThickness}`,
  )
  check(`${label}: safe Z above top`, maxZ > 0 && maxZ <= 60, `maxZ=${maxZ}`)

  // Safe program start: the first motion line must be a rapid, and no
  // feed move may descend below Z0 before any rapid has executed.
  const firstMotion = lines.find((l) => /^G[01]\b/.test(l.trim()))
  check(`${label}: first motion is rapid`, firstMotion?.trim().startsWith('G0') === true, firstMotion)
}

// ---------------------------------------------------------------- job 1: 3D surface

async function surfaceJob(): Promise<void> {
  const stlPath = join(outDir, 'dome.stl')
  writeFileSync(stlPath, domeStl(60, 40, 4, 12))

  const spec: JobSpec = {
    name: 'dome-surface',
    units: 'mm',
    machine: 'fluidnc',
    stock: 'auto',
    stockMargin: 2,
    tools: [
      { id: 'rough', type: 'flat_endmill', diameter: 6, flutes: 2, rpm: 18000, feed: 1500, plungeFeed: 500, stepdown: 2, stepover: 0.45 },
      { id: 'finish', type: 'ball_endmill', diameter: 3, flutes: 2, rpm: 18000, feed: 1200, plungeFeed: 400, stepdown: 1, stepover: 0.12 },
    ],
    features: [{ type: 'mesh', id: 'model', path: stlPath }],
    operations: [
      { kind: 'rough_surface', target: ['model'], tool: 'rough', stockToLeaveAxial: 0.5, stockToLeaveRadial: 0.5 },
      { kind: 'finish_surface', target: ['model'], tool: 'finish', pocketPattern: 'parallel', pocketAngle: 0 },
    ],
    outputPath: join(outDir, 'dome-surface.nc'),
    saveProjectPath: join(outDir, 'dome-surface.camj'),
  }

  const result = await runJob(spec)
  check('surface: job ok', result.ok, result.ok ? '' : result.error)
  if (!result.ok) return
  check('surface: 2 operations', result.operations.length === 2)
  check('surface: rough moves', result.operations[0].moveCount > 50, `moves=${result.operations[0].moveCount}`)
  check('surface: finish moves', result.operations[1].moveCount > 50, `moves=${result.operations[1].moveCount}`)
  checkGcode('surface', result.gcode, 12)
  const warns = result.operations.flatMap((o) => o.warnings)
  if (warns.length) console.log('  note warnings:', JSON.stringify(warns).slice(0, 400))
}

// ---------------------------------------------------------------- job 2: 2.5D

async function profileJob(): Promise<void> {
  const spec: JobSpec = {
    name: 'plate-2d',
    units: 'mm',
    machine: 'grbl',
    stock: { width: 100, height: 80, thickness: 12, material: 'hdpe' },
    tools: [
      { id: 'em6', type: 'flat_endmill', diameter: 6, flutes: 2, rpm: 16000, feed: 900, plungeFeed: 300, stepdown: 3, stepover: 0.4 },
      { id: 'vbit', type: 'v_bit', diameter: 6, vBitAngle: 60, flutes: 2, rpm: 18000, feed: 600, plungeFeed: 250, stepdown: 2, stepover: 0.3 },
      { id: 'drill3', type: 'drill', diameter: 3, flutes: 2, rpm: 12000, feed: 200, plungeFeed: 200, stepdown: 3, stepover: 0 },
    ],
    features: [
      { type: 'rect', id: 'pocket1', x: 10, y: 10, w: 40, h: 30, zTop: 12, zBottom: 8 },
      // Boss inside the pocket: pocket clears around it (island), then the
      // edge route finishes its outer walls. Edge-route targets must be
      // 'add' role — the outline is the material that remains.
      { type: 'rect', id: 'boss', x: 22, y: 18, w: 12, h: 12, zTop: 12, zBottom: 8, role: 'add' },
      { type: 'polygon', id: 'star', points: [[65, 15], [75, 35], [85, 15], [75, 22]], zTop: 12, zBottom: 8, role: 'line' },
      { type: 'circle', id: 'hole1', cx: 30, cy: 55, r: 4, zTop: 12, zBottom: 0 },
    ],
    operations: [
      { kind: 'pocket', target: ['pocket1', 'boss'], tool: 'em6', pocketPattern: 'offset' },
      { kind: 'edge_route_outside', target: ['boss'], tool: 'em6' },
      { kind: 'v_carve', target: ['star'], tool: 'vbit', maxCarveDepth: 4 },
      { kind: 'drilling', target: ['hole1'], tool: 'drill3', drillType: 'peck', peckDepth: 2 },
    ],
    outputPath: join(outDir, 'plate-2d.nc'),
    saveProjectPath: join(outDir, 'plate-2d.camj'),
  }

  const result = await runJob(spec)
  check('2.5D: job ok', result.ok, result.ok ? '' : result.error)
  if (!result.ok) return
  check('2.5D: 4 operations', result.operations.length === 4)
  for (const op of result.operations) {
    check(`2.5D: ${op.kind} produced moves`, op.moveCount > 0, `moves=${op.moveCount}`)
    if (op.warnings.length) console.log(`  note ${op.kind} warnings:`, JSON.stringify(op.warnings).slice(0, 300))
  }
  checkGcode('2.5D', result.gcode, 12)
}

// ---------------------------------------------------------------- main

const start = Date.now()
await surfaceJob()
await profileJob()
console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : failures + ' CHECKS FAILED'} in ${Date.now() - start}ms`)
process.exit(failures === 0 ? 0 : 1)
