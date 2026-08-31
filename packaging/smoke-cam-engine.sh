#!/bin/bash
# Smoke-check a dw-cam-engine sidecar payload (bun runtime + dw-cam-engine.js
# bundle), usable against either a build-tree payload or an installed one.
#
# Usage: packaging/smoke-cam-engine.sh <payload-dir> [port]
#   port default: 18981
#   jobspec for check 3 (POST /api/job): defaults to
#   $SCRIPT_DIR/../cambridge/bridge/examples/dome-fluidnc.json, override with
#   the DW_SMOKE_JOBSPEC env var. If neither exists on disk, check 3 is
#   SKIPPED (not FAILed) — the installed-system smoke has no repo checkout
#   to find the example in.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PAYLOAD_DIR="${1:?usage: smoke-cam-engine.sh <payload-dir> [port]}"
PORT="${2:-18981}"
JOBSPEC="${DW_SMOKE_JOBSPEC:-$SCRIPT_DIR/../cambridge/bridge/examples/dome-fluidnc.json}"

STATUS=0
PID=

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1" >&2; STATUS=1; }

cleanup() {
    if [[ -n "$PID" ]]; then
        kill "$PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

if [[ ! -x "$PAYLOAD_DIR/bun" ]]; then
    fail "payload missing executable bun at $PAYLOAD_DIR/bun"
    echo "SMOKE FAIL"
    exit 1
fi
if [[ ! -f "$PAYLOAD_DIR/dw-cam-engine.js" ]]; then
    fail "payload missing $PAYLOAD_DIR/dw-cam-engine.js"
    echo "SMOKE FAIL"
    exit 1
fi

BASE_URL="http://127.0.0.1:$PORT"

(cd "$PAYLOAD_DIR" && DW_BRIDGE_PORT="$PORT" ./bun dw-cam-engine.js) &
PID=$!

healthy=false
for _ in $(seq 1 20); do
    if ! kill -0 "$PID" 2>/dev/null; then
        fail "dw-cam-engine.js exited before becoming healthy"
        echo "SMOKE FAIL"
        exit 1
    fi
    if health=$(curl -sf "$BASE_URL/api/health" 2>/dev/null); then
        healthy=true
        break
    fi
    sleep 0.25
done

if [[ "$healthy" != true ]]; then
    fail "timed out waiting for $BASE_URL/api/health"
    echo "SMOKE FAIL"
    exit 1
fi

if grep -Fq '"service":"dw-bridge"' <<<"$health"; then
    pass "health response reports service dw-bridge"
else
    fail "health response missing service:dw-bridge ($health)"
fi

machines=$(curl -sf "$BASE_URL/api/machines" 2>/dev/null)
if [[ "$machines" == \[*\] ]] && grep -Fq '"id":"grbl"' <<<"$machines"; then
    pass "/api/machines returns array containing grbl"
else
    fail "/api/machines did not return an array containing grbl ($machines)"
fi

if [[ -f "$JOBSPEC" ]] && command -v python3 >/dev/null; then
    # The committed example uses cambridge-relative feature paths; the bridge
    # resolves paths against its own cwd (the payload dir), so rewrite them to
    # absolute and strip any file-writing fields before POSTing.
    RESOLVED_JOBSPEC=$(mktemp)
    trap 'cleanup; rm -f "$RESOLVED_JOBSPEC"' EXIT
    python3 - "$JOBSPEC" "$SCRIPT_DIR/../cambridge" > "$RESOLVED_JOBSPEC" << 'PY'
import json, os, sys
spec = json.load(open(sys.argv[1]))
cambridge = os.path.realpath(sys.argv[2])
for feature in spec.get("features", []):
    path = feature.get("path")
    if path and not os.path.isabs(path):
        feature["path"] = os.path.join(cambridge, path)
spec.pop("outputPath", None)
spec.pop("saveProjectPath", None)
json.dump(spec, sys.stdout)
PY
    job_response=$(curl -sf -X POST "$BASE_URL/api/job" \
        -H 'Content-Type: application/json' \
        --data-binary @"$RESOLVED_JOBSPEC" 2>/dev/null)
    if grep -Fq '"ok":true' <<<"$job_response" && grep -Fq '"gcode"' <<<"$job_response"; then
        pass "POST /api/job returns ok:true with gcode"
    else
        fail "POST /api/job did not return ok:true/gcode ($job_response)"
    fi
else
    echo "SKIP: POST /api/job (no jobspec at $JOBSPEC, or python3 unavailable)"
fi

if [[ "$STATUS" -eq 0 ]]; then
    echo "SMOKE PASS"
else
    echo "SMOKE FAIL"
fi
exit "$STATUS"
