/**
 * dw-bridge — Project builder.
 *
 * Converts a JobSpec into an authoritative PureCutCNC format-3.0 Project
 * (definitions + lightweight instances + modelAssets), ready for the
 * toolpath generators. Mesh import reuses the engine's own loaders
 * (three STLLoader / hand-rolled OBJ parser) and the silhouette extractor;
 * definition/instance wiring reuses the store's normalization pipeline via
 * the same helper the engine's test fixtures use.
 */

import { readFileSync } from 'node:fs'
import {
  circleProfile,
  defaultOrigin,
  defaultTool,
  getProfileBounds,
  newProject,
  polygonProfile,
  rectProfile,
  type FeatureKind,
  type Operation,
  type Project,
  type Sketch,
  type SketchFeature,
  type Tool,
} from '../vendor/purecutcnc/src/types/project'
import {
  loadImportedTriangleMesh,
  normalizeImportedMeshForStorage,
  serializeImportedMesh,
} from '../vendor/purecutcnc/src/engine/importedMesh'
import { extractImportedMeshProfileAndBounds } from '../vendor/purecutcnc/src/import/stl'
import { projectWithFeatures } from '../vendor/purecutcnc/src/test/projectFixtures'
import { getMachine } from './machines'
import type {
  FeatureSpec,
  JobSpec,
  OperationSpec,
  ToolSpec,
} from './spec'

function emptySketch(profile: Sketch['profile']): Sketch {
  return {
    profile,
    origin: { x: 0, y: 0 },
    orientationAngle: 0,
    dimensions: [],
    constraints: [],
  }
}

function buildTool(spec: ToolSpec, units: 'mm' | 'inch', index: number): Tool {
  const base = defaultTool(units, index)
  return {
    ...base,
    id: spec.id,
    name: spec.name ?? `${spec.type} ${spec.diameter}`,
    type: spec.type,
    diameter: spec.diameter,
    vBitAngle: spec.vBitAngle ?? (spec.type === 'v_bit' ? 60 : null),
    flutes: spec.flutes ?? base.flutes,
    material: spec.material ?? base.material,
    defaultRpm: spec.rpm ?? base.defaultRpm,
    defaultFeed: spec.feed ?? base.defaultFeed,
    defaultPlungeFeed: spec.plungeFeed ?? base.defaultPlungeFeed,
    defaultStepdown: spec.stepdown ?? base.defaultStepdown,
    defaultStepover: spec.stepover ?? base.defaultStepover,
    maxCutDepth: spec.maxCutDepth ?? 0,
  }
}

async function buildMeshFeature(spec: Extract<FeatureSpec, { type: 'mesh' }>, id: string): Promise<SketchFeature> {
  const format = spec.format ?? (spec.path.toLowerCase().endsWith('.obj') ? 'obj' : 'stl')
  const buf = readFileSync(spec.path)
  const data = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength)
  const raw = loadImportedTriangleMesh(format, data, spec.axisSwap ?? 'none')
  if (!raw) {
    throw new Error(`Failed to parse ${format.toUpperCase()} file: ${spec.path}`)
  }
  const mesh = normalizeImportedMeshForStorage(raw, spec.scale ?? 1)
  const info = await extractImportedMeshProfileAndBounds(mesh)
  if (!info) {
    throw new Error(`Failed to extract silhouette from mesh: ${spec.path}`)
  }

  const zTop = spec.zTop ?? info.z_top
  const zBottom = spec.zBottom ?? info.z_bottom

  return {
    id,
    name: spec.name ?? id,
    kind: 'stl' satisfies FeatureKind,
    folderId: null,
    stl: {
      format,
      // Transient embedded mesh — normalizeProject moves this into
      // Project.modelAssets and sets meshAssetId/scale/axisSwap canonically.
      mesh: serializeImportedMesh(mesh, format),
      scale: 1,
      axisSwap: 'none',
      silhouettePaths: info.silhouettePaths,
    },
    sketch: emptySketch(info.profile),
    operation: 'model',
    z_top: zTop,
    z_bottom: zBottom,
    visible: true,
    locked: false,
  }
}

function buildPrimitiveFeature(
  spec: Exclude<FeatureSpec, { type: 'mesh' }>,
  id: string,
): SketchFeature {
  let kind: FeatureKind
  let profile: Sketch['profile']
  if (spec.type === 'rect') {
    kind = 'rect'
    profile = rectProfile(spec.x, spec.y, spec.w, spec.h)
  } else if (spec.type === 'circle') {
    kind = 'circle'
    profile = circleProfile(spec.cx, spec.cy, spec.r)
  } else {
    kind = 'polygon'
    const points = spec.points.map(([x, y]) => ({ x, y }))
    if (spec.closed === false) {
      profile = {
        start: points[0] ?? { x: 0, y: 0 },
        segments: points.slice(1).map((to) => ({ type: 'line' as const, to })),
        closed: false,
      }
    } else {
      profile = polygonProfile(points)
    }
  }

  return {
    id,
    name: spec.name ?? id,
    kind,
    folderId: null,
    sketch: emptySketch(profile),
    operation: spec.role ?? 'subtract',
    z_top: spec.zTop,
    z_bottom: spec.zBottom,
    visible: true,
    locked: false,
  }
}

function buildOperation(spec: OperationSpec, index: number, tools: Tool[]): Operation {
  const tool = tools.find((t) => t.id === spec.tool)
  if (!tool) {
    throw new Error(`Operation ${spec.id ?? index}: unknown tool '${spec.tool}'`)
  }
  const finishKinds = ['finish_surface', 'finish_surface_cleanup']
  return {
    id: spec.id ?? `op${index + 1}`,
    name: spec.name ?? `${spec.kind} ${index + 1}`,
    description: spec.description,
    kind: spec.kind,
    pass: finishKinds.includes(spec.kind) ? 'finish' : 'rough',
    enabled: spec.enabled ?? true,
    showToolpath: true,
    debugToolpath: false,
    target: { source: 'features', featureIds: spec.target },
    toolRef: spec.tool,
    stepdown: spec.stepdown ?? tool.defaultStepdown,
    stepover: spec.stepover ?? tool.defaultStepover,
    feed: spec.feed ?? tool.defaultFeed,
    plungeFeed: spec.plungeFeed ?? tool.defaultPlungeFeed,
    rpm: spec.rpm ?? tool.defaultRpm,
    pocketPattern: spec.pocketPattern ?? 'offset',
    pocketAngle: spec.pocketAngle ?? 0,
    pocketSlotFeedPercent: spec.pocketSlotFeedPercent,
    roundOutsideCorners: spec.roundOutsideCorners,
    stockToLeaveRadial: spec.stockToLeaveRadial ?? 0,
    stockToLeaveAxial: spec.stockToLeaveAxial ?? 0,
    finishWalls: spec.finishWalls ?? true,
    finishFloor: spec.finishFloor ?? true,
    carveDepth: spec.carveDepth ?? 0,
    maxCarveDepth: spec.maxCarveDepth ?? 0,
    cutDirection: spec.cutDirection ?? 'conventional',
    machiningOrder: spec.machiningOrder ?? 'level_first',
    drillType: spec.drillType,
    peckDepth: spec.peckDepth,
    dwellTime: spec.dwellTime,
    retractHeight: spec.retractHeight,
    waterlineAdaptiveRefinement: spec.waterlineAdaptiveRefinement,
    waterlineMicroStepover: spec.waterlineMicroStepover,
    waterlineRefinementThreshold: spec.waterlineRefinementThreshold,
    waterlineMaxRingsPerBand: spec.waterlineMaxRingsPerBand,
    waterlineTipStepdown: spec.waterlineTipStepdown,
    arcFittingEnabled: spec.arcFittingEnabled,
  }
}

/**
 * Build the authoritative Project for a job. Async because mesh silhouette
 * extraction may spin up the manifold WASM module.
 */
export async function buildProject(spec: JobSpec): Promise<Project> {
  const units = spec.units ?? 'mm'
  const project = newProject(spec.name ?? 'dw-job', units)
  project.meta.selectedMachineId = spec.machine

  // Keep the .camj self-contained: when the job targets a machine that is
  // not in the engine's bundled list (e.g. FluidNC), embed its definition
  // so the app UI shows it selected and can post-process with it too.
  if (!project.meta.machineDefinitions.some((d) => d.id === spec.machine)) {
    const extra = getMachine(spec.machine)
    if (extra) {
      project.meta.machineDefinitions.push(extra)
    }
  }

  const tools = spec.tools.map((t, i) => buildTool(t, units, i + 1))

  const rows: SketchFeature[] = []
  let meshRow: SketchFeature | null = null
  let meshIndex = 0
  for (const [i, featureSpec] of spec.features.entries()) {
    const id = featureSpec.id ?? `f${i + 1}`
    if (featureSpec.type === 'mesh') {
      meshIndex += 1
      const row = await buildMeshFeature(featureSpec, id)
      rows.push(row)
      meshRow = row
    } else {
      rows.push(buildPrimitiveFeature(featureSpec, id))
    }
  }
  if (meshIndex > 1) {
    throw new Error('Only one mesh feature per job is supported by dw-bridge')
  }

  if (spec.stock === 'auto') {
    if (!meshRow) {
      throw new Error("stock 'auto' requires a mesh feature")
    }
    const margin = spec.stockMargin ?? 0
    // Derive XY from the mesh silhouette bounds and thickness from its Z span.
    const bounds = getProfileBounds(meshRow.sketch.profile)
    const zTop = typeof meshRow.z_top === 'number' ? meshRow.z_top : 0
    project.stock = {
      profile: rectProfile(
        bounds.minX - margin,
        bounds.minY - margin,
        bounds.maxX - bounds.minX + margin * 2,
        bounds.maxY - bounds.minY + margin * 2,
      ),
      thickness: zTop,
      material: 'auto',
      color: '#b9a83c',
      visible: true,
      origin: { x: 0, y: 0 },
    }
  } else {
    project.stock = {
      profile: rectProfile(0, 0, spec.stock.width, spec.stock.height),
      thickness: spec.stock.thickness,
      material: spec.stock.material ?? 'unspecified',
      color: '#b9a83c',
      visible: true,
      origin: { x: 0, y: 0 },
    }
  }

  // Origin at stock front-left-top: machine Z0 = stock top surface.
  project.origin = defaultOrigin(project.stock)

  const operations = spec.operations.map((op, i) => buildOperation(op, i, tools))

  return projectWithFeatures(
    {
      ...project,
      tools,
      operations,
    },
    rows,
  )
}
