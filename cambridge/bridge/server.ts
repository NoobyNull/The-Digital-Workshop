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

const PORT = Number(process.env.DW_BRIDGE_PORT ?? 8973)
const HOST = '127.0.0.1'
const MAX_BODY_BYTES = 128 * 1024 * 1024

function sendJson(res: ServerResponse, status: number, body: unknown): void {
  const payload = JSON.stringify(body)
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
  })
  res.end(payload)
}

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let size = 0
    req.on('data', (chunk: Buffer) => {
      size += chunk.length
      if (size > MAX_BODY_BYTES) {
        reject(new Error('Request body too large'))
        req.destroy()
        return
      }
      chunks.push(chunk)
    })
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    req.on('error', reject)
  })
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
      let spec: JobSpec
      try {
        spec = JSON.parse(raw) as JobSpec
      } catch {
        sendJson(res, 400, { ok: false, error: 'Request body is not valid JSON' })
        return
      }
      const result = await runJob(spec)
      sendJson(res, result.ok ? 200 : 422, result)
      return
    }

    sendJson(res, 404, { ok: false, error: 'Not found' })
  } catch (err) {
    sendJson(res, 500, {
      ok: false,
      error: err instanceof Error ? err.message : String(err),
    })
  }
})

server.listen(PORT, HOST, () => {
  console.log(`dw-bridge listening on http://${HOST}:${PORT}`)
})
