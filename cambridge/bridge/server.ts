/**
 * dw-bridge — localhost JSON/HTTP sidecar for Digital Workshop.
 *
 * Zero-dependency (node:http) API wrapping the PureCutCNC CAM engine so
 * native applications (e.g. the C++ Digital Workshop app) can generate
 * G-code with plain HTTP calls.
 *
 *   GET  /api/health     -> { ok, service, version }
 *   GET  /api/machines   -> [{ id, name, description, fileExtension }]
 *   POST /api/job        -> JobSpec in, JobResult out (see spec.ts)
 *
 * Port: DW_BRIDGE_PORT env or 8973. Bind address 127.0.0.1 only — this is a
 * local sidecar, not a network service.
 */

import { createServer, type IncomingMessage, type ServerResponse } from 'node:http'
import { listMachines } from './machines'
import { runJob } from './runner'
import type { JobSpec } from './spec'

function portFromEnv(): number {
  const raw = process.env.DW_BRIDGE_PORT
  if (raw === undefined || raw === '') return 8973
  const parsed = Number(raw)
  if (!Number.isInteger(parsed) || parsed < 1 || parsed > 65535) {
    console.error(`dw-bridge: ignoring invalid DW_BRIDGE_PORT=${raw}`)
    return 8973
  }
  return parsed
}

const PORT = portFromEnv()
const HOST = '127.0.0.1'
// JobSpecs are small structured JSON — meshes travel by path, not inline.
const MAX_BODY_BYTES = 8 * 1024 * 1024

function sendJson(res: ServerResponse, status: number, body: unknown): void {
  const payload = JSON.stringify(body)
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
  })
  res.end(payload)
}

class HttpError extends Error {
  constructor(readonly status: number, message: string) {
    super(message)
  }
}

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let size = 0
    req.on('data', (chunk: Buffer) => {
      size += chunk.length
      if (size > MAX_BODY_BYTES) {
        // Stop buffering but keep the socket alive so the handler can still
        // deliver the 413 — destroying here would reset the connection
        // before the client sees any error.
        req.removeAllListeners('data')
        req.resume()
        reject(new HttpError(413, 'Request body too large'))
        return
      }
      chunks.push(chunk)
    })
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    req.on('error', reject)
  })
}

// Cheap shape check at the trust boundary: reject wrong-shaped JSON with a
// clear 400 instead of a TypeError from deep inside the pipeline.
function looksLikeJobSpec(value: unknown): value is JobSpec {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return false
  const spec = value as Record<string, unknown>
  return (
    typeof spec.machine === 'string' &&
    Array.isArray(spec.tools) &&
    Array.isArray(spec.features) &&
    Array.isArray(spec.operations)
  )
}

const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url ?? '/', `http://${HOST}`)

    if (req.method === 'GET' && url.pathname === '/api/health') {
      sendJson(res, 200, { ok: true, service: 'dw-bridge', version: 1 })
      return
    }

    if (req.method === 'GET' && url.pathname === '/api/machines') {
      sendJson(
        res,
        200,
        listMachines().map((m) => ({
          id: m.id,
          name: m.name,
          description: m.description,
          fileExtension: m.fileExtension,
        })),
      )
      return
    }

    if (req.method === 'POST' && url.pathname === '/api/job') {
      const raw = await readBody(req)
      let parsed: unknown
      try {
        parsed = JSON.parse(raw)
      } catch {
        sendJson(res, 400, { ok: false, error: 'Request body is not valid JSON' })
        return
      }
      if (!looksLikeJobSpec(parsed)) {
        sendJson(res, 400, {
          ok: false,
          error: 'JobSpec must be an object with machine, tools[], features[], operations[]',
        })
        return
      }
      const result = await runJob(parsed)
      sendJson(res, result.ok ? 200 : 422, result)
      return
    }

    sendJson(res, 404, { ok: false, error: 'Not found' })
  } catch (err) {
    const status = err instanceof HttpError ? err.status : 500
    sendJson(res, status, {
      ok: false,
      error: err instanceof Error ? err.message : String(err),
    })
  }
})

server.on('error', (err) => {
  console.error(`dw-bridge failed to listen on http://${HOST}:${PORT}: ${err.message}`)
  process.exit(1)
})

server.listen(PORT, HOST, () => {
  console.log(`dw-bridge listening on http://${HOST}:${PORT}`)
})
