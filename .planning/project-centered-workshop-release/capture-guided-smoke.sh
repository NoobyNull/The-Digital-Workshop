#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
release_dir="$repo_root/.planning/project-centered-workshop-release"
binary="${1:-$repo_root/build/digital_workshop}"
helper="$release_dir/capture-guided-smoke-one.sh"
template="$release_dir/guided-smoke-config.ini"

if [[ ! -x "$binary" ]]; then
    echo "Digital Workshop binary is not executable: $binary" >&2
    exit 1
fi

for dimensions in 1366x768 3840x2160; do
    width=${dimensions%x*}
    height=${dimensions#*x}
    for scale in 1.0 1.5 2.0; do
        name="guided-${dimensions}-scale-${scale}.png"
        xvfb-run -a -s "-screen 0 ${dimensions}x24 -dpi 96" \
            "$helper" "$binary" "$template" "$release_dir/$name" \
            "$width" "$height" "$scale"
    done
done
