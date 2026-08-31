/**
 * dw-bridge — job runner.
 *
 * Synchronous, headless equivalent of the app's toolpath pipeline
 * (src/app/useToolpathGeneration.ts) followed by G-code post-processing
 * (src/components/export/ExportDialog.tsx). Same wrapper order:
 * tabs -> tab warnings -> linear-move optimization -> clamp warnings,
 * then runPostProcessor over the enabled operations in spec order.
 */

import { writeFileSync } from 'node:fs'
import {
  applyClampWarnings,
  applyTabsToEdgeRoute,
  applyTabWarnings,
  generateDrillingToolpath,
  generateEdgeRouteToolpath,
  generateFinishSurfaceCleanupToolpath,
  generateFinishSurfaceToolpath,
  generateFollowLineToolpath,
  generatePocketToolpath,
  generateRoughSurfaceToolpath,
  generateSurfaceCleanToolpath,
  generateVCarveMedialToolpath,
  generateVCarveToolpath,
  optimizeLinearMoves,
  type ToolpathResult,
} from '../vendor/purecutcnc/src/engine/toolpaths'
import { normalizeToolForProject, getOperationSafeZ } from '../vendor/purecutcnc/src/engine/toolpaths/geometry'
import { runPostProcessor } from '../vendor/purecutcnc/src/engine/gcode/postprocessor'
import type { Operation, Project } from '../vendor/purecutcnc/src/types/project'
import { buildProject } from './builder'
import { getMachine } from './machines'
import type { JobResult, JobSpec } from './spec'

/**
 * Safe program start (bridge-level, pre-post transformation).
 *
 * The engine marks each operation's entry point with a zero-length rapid
 * (from == to), which the arc-fitting postprocessor drops as degenerate —
 * so the program's first emitted motion would otherwise be a diagonal
 * plunge from the machine's unknown parking position. Anchor the start by
 * giving that first rapid a real travel: the post then emits
 * `G0 Z<safe>` followed by `G0 X<entry> Y<entry>` (Z-first splitting is the
 * postprocessor's own emitRapid behavior). The substituted `from` is never
 * emitted — it only defeats the degenerate-move filter. If the first move
 * is not a rapid at all, prepend the anchor instead.
 */
function anchorProgramStart(project: Project, toolpath: ToolpathResult): ToolpathResult {
  const first = toolpath.moves[0]
  if (!first) return toolpath

  if (first.kind === 'rapid') {
    const degenerate =
      first.from.x === first.to.x && first.from.y === first.to.y && first.from.z === first.to.z
    if (!degenerate) return toolpath
    const moves = [...toolpath.moves]
    moves[0] = { ...first, from: { x: 0, y: 0, z: first.to.z } }
    return { ...toolpath, moves }
  }

  const safeZ = getOperationSafeZ(project)
  const anchor = {
    kind: 'rapid' as const,
    from: { x: 0, y: 0, z: safeZ },
    to: { x: first.from.x, y: first.from.y, z: safeZ },
  }
  return { ...toolpath, moves: [anchor, ...toolpath.moves] }
}

function generateToolpath(project: Project, operation: Operation): ToolpathResult | null {
  let result: ToolpathResult | null = null

  if (operation.kind === 'pocket') {
    result = applyTabWarnings(project, operation, generatePocketToolpath(project, operation))
  } else if (operation.kind === 'v_carve') {
    result = generateVCarveToolpath(project, operation)
  } else if (operation.kind === 'v_carve_medial') {
    result = generateVCarveMedialToolpath(project, operation)
  } else if (operation.kind === 'edge_route_inside' || operation.kind === 'edge_route_outside') {
    result = applyTabWarnings(project, operation, applyTabsToEdgeRoute(project, operation, generateEdgeRouteToolpath(project, operation)))
  } else if (operation.kind === 'surface_clean') {
    result = applyTabWarnings(project, operation, generateSurfaceCleanToolpath(project, operation))
  } else if (operation.kind === 'rough_surface') {
    result = applyTabWarnings(project, operation, generateRoughSurfaceToolpath(project, operation))
  } else if (operation.kind === 'finish_surface') {
    result = applyTabWarnings(project, operation, applyTabsToEdgeRoute(project, operation, generateFinishSurfaceToolpath(project, operation)))
  } else if (operation.kind === 'finish_surface_cleanup') {
    result = applyTabWarnings(project, operation, applyTabsToEdgeRoute(project, operation, generateFinishSurfaceCleanupToolpath(project, operation)))
  } else if (operation.kind === 'follow_line') {
    result = generateFollowLineToolpath(project, operation)
  } else if (operation.kind === 'drilling') {
    result = generateDrillingToolpath(project, operation)
  }

  if (!result) {
    return null
  }
  return applyClampWarnings(project, optimizeLinearMoves(result), operation)
}

export async function runJob(spec: JobSpec): Promise<JobResult> {
  try {
    const definition = getMachine(spec.machine)
    if (!definition) {
      return { ok: false, error: `Unknown machine '${spec.machine}' — see GET /api/machines` }
    }
    if (!spec.operations.length) {
      return { ok: false, error: 'Job has no operations' }
    }

    const project = await buildProject(spec)

    const active = []
    const reports = []
    for (const operation of project.operations) {
      if (!operation.enabled) continue
      const toolpath = generateToolpath(project, operation)
      const tool = project.tools.find((t) => t.id === operation.toolRef)
      if (!toolpath || !tool) {
        reports.push({
          id: operation.id,
          kind: operation.kind,
          name: operation.name,
          moveCount: 0,
          warnings: [{ code: 'dwBridge.noToolpath', params: {} }],
        })
        continue
      }
      reports.push({
        id: operation.id,
        kind: operation.kind,
        name: operation.name,
        moveCount: toolpath.moves.length,
        warnings: toolpath.warnings,
      })
      active.push({ operation, tool: normalizeToolForProject(tool, project), toolpath })
    }

    if (active.length === 0) {
      return { ok: false, error: 'No operation produced a toolpath', details: reports }
    }

    active[0].toolpath = anchorProgramStart(project, active[0].toolpath)

    // Fail fast on silent operations: emitting partial G-code for a CNC job
    // is worse than failing the whole job.
    const empty = reports.filter((r) => r.moveCount === 0)
    if (empty.length > 0) {
      return {
        ok: false,
        error: `${empty.length} operation(s) produced no moves: ${empty.map((r) => `${r.name} (${r.kind})`).join(', ')}`,
        details: empty,
      }
    }

    const result = runPostProcessor({
      project,
      operations: active,
      definition,
      options: {
        emitToolChanges: spec.options?.emitToolChanges ?? true,
        emitCoolant: spec.options?.emitCoolant ?? false,
        programName: spec.options?.programName ?? spec.name ?? project.meta.name,
      },
    })

    const files: { gcode?: string; camj?: string } = {}
    if (spec.outputPath) {
      writeFileSync(spec.outputPath, result.gcode)
      files.gcode = spec.outputPath
    }
    if (spec.saveProjectPath) {
      // Compact: .camj is machine-read and embeds serialized mesh assets, so
      // pretty-printing only inflates multi-MB files.
      writeFileSync(spec.saveProjectPath, JSON.stringify(project))
      files.camj = spec.saveProjectPath
    }

    return {
      ok: true,
      gcode: result.gcode,
      stats: result.stats,
      warnings: result.warnings,
      operations: reports,
      ...(files.gcode || files.camj ? { files } : {}),
    }
  } catch (err) {
    return {
      ok: false,
      error: err instanceof Error ? err.message : String(err),
    }
  }
}
