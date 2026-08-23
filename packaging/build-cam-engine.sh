#!/bin/bash
# Build the dw-cam-engine sidecar payload from the cambridge environment.
# Usage: packaging/build-cam-engine.sh <out-dir> [--platform linux-x64]
# Output layout (proven by the Phase 2 PoC, commit a2a7ac3):
#   <out-dir>/bun                      runtime binary
#   <out-dir>/dw-cam-engine.js        bundled bridge (+engine), manifold external
#   <out-dir>/node_modules/manifold-3d/  wasm + glue, external because the
#                                        bundler breaks its emscripten glue
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(realpath "$SCRIPT_DIR/..")"
CAMBRIDGE="$REPO_ROOT/cambridge"
OUT="${1:?usage: build-cam-engine.sh <out-dir> [--platform linux-x64]}"
OUT="$(realpath -m "$OUT")"
case "$OUT" in
    "$REPO_ROOT"|"$HOME"|/) echo "refusing to build into $OUT"; exit 1 ;;
esac
PLATFORM="${3:-linux-x64}"
[ "${2:-}" = "--platform" ] && PLATFORM="$3"

command -v bun >/dev/null || { echo "bun is required (https://bun.sh)"; exit 1; }
[ -d "$CAMBRIDGE/node_modules/manifold-3d" ] || { echo "run npm install in cambridge/ first"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/node_modules"
(cd "$CAMBRIDGE" && bun build --target=bun --external manifold-3d \
    bridge/server.ts --outfile "$OUT/dw-cam-engine.js")
cp -a "$CAMBRIDGE/node_modules/manifold-3d" "$OUT/node_modules/"

if [ "$PLATFORM" = "linux-x64" ]; then
    cp "$(command -v bun)" "$OUT/bun"
else
    # ponytail: non-Linux runtimes are fetched in CI, not here.
    echo "NOTE: $PLATFORM runtime not staged; download bun-$PLATFORM in CI." >&2
fi

echo "cam-engine payload: $OUT ($(du -sh "$OUT" | cut -f1))"
