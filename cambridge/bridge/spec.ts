/**
 * dw-bridge — job specification model.
 *
 * A JobSpec is the single JSON document that describes a complete CNC job:
 * units, stock, tools, geometry features, machining operations, and the
 * target controller. This is the contract DW (or any client) sends to the
 * bridge. Z coordinates are absolute project Z: stock bottom = 0,
 * stock top = stock.thickness.
 */

export type Units = 'mm' | 'inch'

export type FeatureRole = 'add' | 'subtract' | 'line'

export interface ToolSpec {
  id: string
  name?: string
  type: 'flat_endmill' | 'ball_endmill' | 'v_bit' | 'drill'
  diameter: number
  vBitAngle?: number | null
  flutes?: number
  material?: 'hss' | 'carbide'
  rpm?: number
  feed?: number
  plungeFeed?: number
  stepdown?: number
  stepover?: number
  maxCutDepth?: number
}

export interface MeshFeatureSpec {
  type: 'mesh'
  id?: string
  name?: string
  /** Absolute path to an STL or OBJ file on the bridge host. */
  path: string
  /** 'stl' | 'obj' — inferred from file extension when omitted. */
  format?: 'stl' | 'obj'
  /** Multiplier applied to raw file coordinates (e.g. 25.4 for inch-authored
   *  files in an mm project). Default 1. */
  scale?: number
  /** Axis re-orientation for models authored with a different up axis. */
  axisSwap?: 'none' | 'yz' | 'xz' | 'xy'
  /** Absolute Z of the model top. Default: scaled mesh height (bottom at 0).
   *  Setting zTop/zBottom different from the mesh bounds scales the model
   *  in Z to fit the band (engine clamps Z scale to >= 0.1). */
  zTop?: number
  zBottom?: number
}

export interface RectFeatureSpec {
  type: 'rect'
  id?: string
  name?: string
  x: number
  y: number
  w: number
  h: number
  role?: FeatureRole
  zTop: number
  zBottom: number
}

export interface CircleFeatureSpec {
  type: 'circle'
  id?: string
  name?: string
  cx: number
  cy: number
  r: number
  role?: FeatureRole
  zTop: number
  zBottom: number
}

export interface PolygonFeatureSpec {
  type: 'polygon'
  id?: string
  name?: string
  points: Array<[number, number]>
  /** Default true. Set false for open polylines (follow_line engraving). */
  closed?: boolean
  role?: FeatureRole
  zTop: number
  zBottom: number
}

export type FeatureSpec =
  | MeshFeatureSpec
  | RectFeatureSpec
  | CircleFeatureSpec
  | PolygonFeatureSpec

export type OperationKindSpec =
  | 'pocket'
  | 'v_carve'
  | 'v_carve_medial'
  | 'edge_route_inside'
  | 'edge_route_outside'
  | 'surface_clean'
  | 'rough_surface'
  | 'finish_surface'
  | 'finish_surface_cleanup'
  | 'follow_line'
  | 'drilling'

export interface OperationSpec {
  kind: OperationKindSpec
  /** Feature ids this operation machines. */
  target: string[]
  /** Tool id from the tools array. */
  tool: string
  id?: string
  name?: string
  description?: string
  enabled?: boolean
  stepdown?: number
  stepover?: number
  feed?: number
  plungeFeed?: number
  rpm?: number
  pocketPattern?: 'offset' | 'parallel' | 'waterline'
  pocketAngle?: number
  pocketSlotFeedPercent?: number
  roundOutsideCorners?: boolean
  stockToLeaveRadial?: number
  stockToLeaveAxial?: number
  finishWalls?: boolean
  finishFloor?: boolean
  carveDepth?: number
  maxCarveDepth?: number
  cutDirection?: 'conventional' | 'climb'
  machiningOrder?: 'level_first' | 'feature_first'
  drillType?: 'simple' | 'peck' | 'dwell' | 'chip_breaking'
  peckDepth?: number
  dwellTime?: number
  retractHeight?: number
  waterlineAdaptiveRefinement?: boolean
  waterlineMicroStepover?: number
  waterlineRefinementThreshold?: number
  waterlineMaxRingsPerBand?: number
  waterlineTipStepdown?: number
  arcFittingEnabled?: boolean
}

export interface StockSpec {
  width: number
  height: number
  thickness: number
  material?: string
}

export interface JobSpec {
  name?: string
  units?: Units
  /** Postprocessor id: generic | grbl | grblhal | fluidnc | linuxcnc | mach3 | uccnc */
  machine: string
  /** Explicit stock, or 'auto' to derive from the mesh bounding box
   *  (requires exactly one mesh feature). Auto thickness = model zTop. */
  stock: StockSpec | 'auto'
  /** Margin added around the mesh bbox when stock is 'auto'. Default 0. */
  stockMargin?: number
  tools: ToolSpec[]
  features: FeatureSpec[]
  operations: OperationSpec[]
  /** G-code emission options. */
  options?: {
    emitToolChanges?: boolean
    emitCoolant?: boolean
    programName?: string
  }
  /** When set, the G-code is also written to this absolute path. */
  outputPath?: string
  /** When set, the generated .camj project is written to this absolute path. */
  saveProjectPath?: string
}

export interface OperationReport {
  id: string
  kind: string
  name: string
  moveCount: number
  warnings: unknown[]
}

export interface JobSuccess {
  ok: true
  gcode: string
  stats: { lineCount: number; operationCount: number; moveCount: number }
  warnings: unknown[]
  operations: OperationReport[]
  files?: { gcode?: string; camj?: string }
}

export interface JobFailure {
  ok: false
  error: string
  details?: unknown
}

export type JobResult = JobSuccess | JobFailure
