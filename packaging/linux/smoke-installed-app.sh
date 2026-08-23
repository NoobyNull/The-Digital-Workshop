#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <digital-workshop-binary> <version> <log-file>" >&2
    exit 2
fi

binary=$(realpath "$1")
version=$2
log_file=$(realpath -m "$3")
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
    echo "Installed binary is not executable: $binary" >&2
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
    "$binary" --verbose >"$log_file" 2>&1 &
app_pid=$!

initialized=false
for _ in $(seq 1 80); do
    if ! kill -0 "$app_pid" 2>/dev/null; then
        wait "$app_pid" || true
        echo "Digital Workshop exited before initialization; see $log_file" >&2
        exit 1
    fi
    if grep -Fq "Digital Workshop $version (" "$log_file"; then
        initialized=true
        break
    fi
    sleep 0.25
done

if [[ "$initialized" != true ]]; then
    echo "Timed out waiting for Digital Workshop $version; see $log_file" >&2
    exit 1
fi

grep -Fq "GraphQLite extension loaded successfully" "$log_file"
grep -Fq "Database schema initialized successfully" "$log_file"
# Console logs include an ANSI color reset between the component and message.
grep -Eq "ToolDatabase].*Opened:" "$log_file"
if grep -Eiq "duplicate column|failed to initialize database schema|ToolDatabase.*(failed|error|cannot)" \
        "$log_file"; then
    echo "Database startup regression detected; see $log_file" >&2
    exit 1
fi

kill -TERM "$app_pid"
wait "$app_pid" || true
app_pid=

echo "PASS installed startup: $binary"
