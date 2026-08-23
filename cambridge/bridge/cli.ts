/**
 * dw-bridge — command-line front end.
 *
 *   npx tsx dw-bridge/cli.ts job.json [--out job.nc] [--camj job.camj]
 *
 * Runs the same pipeline as the HTTP sidecar without a server. The job JSON
 * may itself contain outputPath/saveProjectPath; the CLI flags override.
 */

import { readFileSync, writeFileSync } from 'node:fs'
import { runJob } from './runner'
import type { JobSpec } from './spec'

async function main(): Promise<void> {
  const args = process.argv.slice(2)
  const jobPath = args[0]
  if (!jobPath) {
    console.error('usage: tsx dw-bridge/cli.ts job.json [--out job.nc] [--camj job.camj]')
    process.exit(2)
  }
  const outIdx = args.indexOf('--out')
  const camjIdx = args.indexOf('--camj')

  const spec = JSON.parse(readFileSync(jobPath, 'utf8')) as JobSpec
  if (outIdx !== -1 && args[outIdx + 1]) spec.outputPath = args[outIdx + 1]
  if (camjIdx !== -1 && args[camjIdx + 1]) spec.saveProjectPath = args[camjIdx + 1]

  const result = await runJob(spec)
  if (!result.ok) {
    console.error(`job failed: ${result.error}`)
    if (result.details) console.error(JSON.stringify(result.details, null, 2))
    process.exit(1)
  }

  if (!spec.outputPath) {
    writeFileSync(1, result.gcode)
  }
  console.error(
    `ok: ${result.stats.lineCount} lines, ${result.stats.moveCount} moves, ` +
      `${result.operations.length} operation(s)` +
      (result.files?.gcode ? ` -> ${result.files.gcode}` : '') +
      (result.files?.camj ? `, project -> ${result.files.camj}` : ''),
  )
  for (const op of result.operations) {
    const warn = op.warnings.length ? ` warnings=${JSON.stringify(op.warnings)}` : ''
    console.error(`  ${op.id} ${op.kind}: ${op.moveCount} moves${warn}`)
  }
}

void main()
