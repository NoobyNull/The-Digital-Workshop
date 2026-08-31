#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "Usage: $0 <binary> <config-template> <output.png> <width> <height> <scale>" >&2
    exit 2
fi

binary=$(realpath "$1")
template=$(realpath "$2")
output=$(realpath -m "$3")
width=$4
height=$5
scale=$6
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

mkdir -p "$profile/config/digitalworkshop" "$profile/data" \
    "$profile/cache" "$profile/runtime" "$(dirname "$output")"
chmod 700 "$profile/runtime"
install -m 600 "$template" "$profile/config/digitalworkshop/config.ini"
sed -i \
    -e "s/\[WIDTH\]/$width/g" \
    -e "s/\[HEIGHT\]/$height/g" \
    -e "s/\[SCALE\]/$scale/g" \
    "$profile/config/digitalworkshop/config.ini"

log_file="$profile/capture.log"
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

for _ in $(seq 1 40); do
    if ! kill -0 "$app_pid" 2>/dev/null; then
        wait "$app_pid"
        echo "Digital Workshop exited before capture; see $log_file" >&2
        exit 1
    fi
    if grep -q "initialized" "$log_file"; then
        break
    fi
    sleep 0.25
done

grep -q "initialized" "$log_file"
sleep 1
import -silent -window root "$output"
identify -format '%f %wx%h mean=%[fx:mean] entropy=%[entropy]\n' "$output"

kill -TERM "$app_pid"
wait "$app_pid"
app_pid=
