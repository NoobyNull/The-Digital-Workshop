/**
 * dw-bridge — machine definition registry.
 *
 * Wraps PureCutCNC's bundled postprocessor definitions and adds a FluidNC
 * definition (not shipped upstream). Definitions are validated through the
 * engine's own zod schema before use.
 *
 * PureCutCNC is Apache-2.0 licensed (see repository root LICENSE/NOTICE).
 */

import { BUNDLED_DEFINITIONS } from '../vendor/purecutcnc/src/engine/gcode/definitions'
import { validateMachineDefinition, type MachineDefinition } from '../vendor/purecutcnc/src/engine/gcode/types'
// Imported (not readFileSync) so single-file compiles embed the definition.
import fluidncJson from './machines/fluidnc.json'

function loadFluidnc(): MachineDefinition {
  return validateMachineDefinition(fluidncJson)
}

let cached: MachineDefinition[] | null = null

export function listMachines(): MachineDefinition[] {
  if (!cached) {
    cached = [...BUNDLED_DEFINITIONS, loadFluidnc()]
  }
  return cached
}

export function getMachine(id: string): MachineDefinition | undefined {
  return listMachines().find((machine) => machine.id === id)
}
