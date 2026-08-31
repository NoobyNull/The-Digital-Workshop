/**
 * dw-bridge — .camj app-compatibility verification.
 *
 * Loads a bridge-generated .camj through the SAME strict decoder the
 * PureCutCNC app uses when opening a file (decodeProjectFormat), then
 * reports what the UI would see: feature tree, definitions, model assets,
 * operations, and whether the selected machine resolves.
 *
 *   npx tsx bridge/test/verify-camj.ts <file.camj>
 */

import { readFileSync } from 'node:fs'
import { decodeProjectFormat } from '../../vendor/purecutcnc/src/store/helpers/projectFormat'
import { getActiveMachineDefinition } from '../../vendor/purecutcnc/src/engine/gcode/definitions'
import { resolvedProjectFeatures } from '../../vendor/purecutcnc/src/store/helpers/resolveFeatures'

const path = process.argv[2]
if (!path) {
  console.error('usage: npx tsx bridge/test/verify-camj.ts <file.camj>')
  process.exit(2)
}

const raw = JSON.parse(readFileSync(path, 'utf8'))
const decoded = decodeProjectFormat(raw)
const project = decoded.project

let failures = 0
const check = (name: string, ok: boolean, detail = ''): void => {
  console.log(`${ok ? '  ok ' : 'FAIL '} ${name}${detail ? ' — ' + detail : ''}`)
  if (!ok) failures += 1
}

console.log(`file: ${path}`)
console.log(`version: ${project.version}  compatWarnings: ${decoded.warnings?.length ?? 0}`)

check('decodes without throwing', true)
check('version is 3.0', project.version === '3.0', project.version)

const resolved = resolvedProjectFeatures(project)
check('feature instances resolve', resolved.length === project.features.length,
  `${resolved.length}/${project.features.length}`)

for (const f of resolved) {
  const meshState = f.stl?.meshAssetId
    ? (project.modelAssets[f.stl.meshAssetId] ? 'asset ok' : 'ASSET MISSING')
    : 'no mesh'
  console.log(`     feature "${f.name}" kind=${f.kind} op=${f.operation} z=[${f.z_bottom}..${f.z_top}] ${meshState}`)
}

check('featureTree covers all instances', project.featureTree.length >= project.features.length,
  `tree=${project.featureTree.length} instances=${project.features.length}`)
check('operations present', project.operations.length > 0, `${project.operations.length}`)
check('all operation targets resolve',
  project.operations.every((op) =>
    op.target.source !== 'features' ||
    op.target.featureIds.every((id) => project.features.some((f) => f.id === id))))
check('all operation tools resolve',
  project.operations.every((op) => op.toolRef && project.tools.some((t) => t.id === op.toolRef)))

const machine = getActiveMachineDefinition(project)
check('selected machine resolves', machine !== null,
  `${project.meta.selectedMachineId} -> ${machine?.name ?? 'NOT FOUND'}`)

console.log(failures === 0 ? '\nAPP-COMPATIBLE' : `\n${failures} PROBLEM(S)`)
process.exit(failures === 0 ? 0 : 1)
