#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <dw-settings-binary> <log-file>" >&2
    exit 2
fi

binary=$(realpath "$1")
log_file=$(realpath -m "$2")
profile=$(mktemp -d)
app_pid=

cleanup() {
    if [[ -n "$app_pid" ]] && kill -0 "$app_pid" 2>/dev/null; then
        kill -TERM "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    rm -rf "$profile"
}
trap cleanup EXIT

if [[ ! -x "$binary" ]]; then
    echo "Installed settings binary is not executable: $binary" >&2
    exit 1
fi

mkdir -p "$(dirname "$log_file")" "$profile/config" "$profile/data" \
    "$profile/cache" "$profile/runtime" "$profile/home"
chmod 700 "$profile/runtime"

env \
    HOME="$profile/home" \
    XDG_CONFIG_HOME="$profile/config" \
    XDG_DATA_HOME="$profile/data" \
    XDG_CACHE_HOME="$profile/cache" \
    XDG_RUNTIME_DIR="$profile/runtime" \
    SDL_VIDEODRIVER=x11 \
    LIBGL_ALWAYS_SOFTWARE=1 \
    "$binary" >"$log_file" 2>&1 &
app_pid=$!

# dw_settings has no diagnostic flag. Remaining alive after SDL, OpenGL, and
# ImGui initialization proves that it entered its event loop.
for _ in $(seq 1 8); do
    sleep 0.25
    if ! kill -0 "$app_pid" 2>/dev/null; then
        wait "$app_pid" || true
        echo "dw_settings exited during startup; see $log_file" >&2
        exit 1
    fi
done

kill -TERM "$app_pid"
wait "$app_pid" 2>/dev/null || true
app_pid=

echo "PASS installed settings startup: $binary"
