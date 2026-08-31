#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <build-dir> <version> [evidence-dir]" >&2
    exit 2
fi

build_dir=$(realpath "$1")
version=$2
script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(realpath "$script_dir/../..")
source_resources=$(realpath "$script_dir/../../resources")
evidence_dir=$(realpath -m "${3:-$build_dir/package-smoke-$version}")
tgz="$build_dir/DigitalWorkshop-$version-Linux.tar.gz"
run="$build_dir/DigitalWorkshop-$version-linux.run"
work=$(mktemp -d)

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT

material_manifest() {
    local root=$1
    (
        cd "$root"
        find . -maxdepth 1 -type f -name '*.dwmat' -print0 \
            | sort -z \
            | xargs -0 -r sha256sum
    )
}

icon_manifest() {
    local root=$1
    (
        cd "$root"
        find . -maxdepth 1 -type f \( -name '*.png' -o -name '*.ico' \) -print0 \
            | sort -z \
            | xargs -0 -r sha256sum
    )
}

assert_resources_match() {
    local actual=$1
    local label=$2
    local expected_materials
    local expected_icons

    if [[ ! -d "$actual/materials" || ! -d "$actual/icons" ]]; then
        echo "$label is missing its materials or icons directory" >&2
        exit 1
    fi
    expected_materials=$(material_manifest "$source_resources/materials")
    expected_icons=$(icon_manifest "$source_resources/icons")
    if [[ -z "$expected_materials" || -z "$expected_icons" ]]; then
        echo "Canonical source resources are empty" >&2
        exit 1
    fi
    if ! diff -u <(printf '%s\n' "$expected_materials") \
            <(material_manifest "$actual/materials"); then
        echo "$label bundled materials differ from canonical source resources" >&2
        exit 1
    fi
    if ! diff -u <(printf '%s\n' "$expected_icons") \
            <(icon_manifest "$actual/icons"); then
        echo "$label bundled icons differ from canonical source resources" >&2
        exit 1
    fi
}

for artifact in "$tgz" "$run"; do
    if [[ ! -s "$artifact" ]]; then
        echo "Missing package: $artifact" >&2
        exit 1
    fi
done
if [[ ! -x "$run" ]]; then
    echo "Installer is not executable: $run" >&2
    exit 1
fi

mkdir -p "$evidence_dir"

tar_root="$work/tgz"
mkdir -p "$tar_root"
tar -xzf "$tgz" -C "$tar_root"
tar_package_root="$tar_root/DigitalWorkshop-$version-Linux"
tar_binary="$tar_package_root/bin/digital_workshop"
tar_settings="$tar_package_root/bin/dw_settings"
tar_graphqlite="$tar_package_root/bin/graphqlite.so"
test -x "$tar_binary"
test -x "$tar_settings"
test -s "$tar_graphqlite"
assert_resources_match \
    "$tar_package_root/share/digitalworkshop/resources" "TGZ"

xvfb-run -a -s '-screen 0 1366x768x24 -dpi 96' \
    "$script_dir/smoke-installed-app.sh" "$tar_binary" "$version" \
    "$evidence_dir/tgz-startup.log"
xvfb-run -a -s '-screen 0 1366x768x24 -dpi 96' \
    "$script_dir/smoke-settings-app.sh" "$tar_settings" \
    "$evidence_dir/tgz-settings-startup.log"

run_home="$work/run-home"
mkdir -p "$run_home"
HOME="$run_home" "$run" >"$evidence_dir/run-install.log" 2>&1
run_binary="$run_home/.local/bin/digital_workshop"
run_settings="$run_home/.local/bin/dw_settings"
run_uninstaller="$run_home/.local/share/digitalworkshop/uninstall.sh"
run_desktop="$run_home/.local/share/applications/digitalworkshop.desktop"
test -x "$run_binary"
test -x "$run_settings"
test -s "$run_home/.local/bin/graphqlite.so"
if [[ ! -x "$run_uninstaller" ]]; then
    echo ".run install did not provide a persistent uninstaller: $run_uninstaller" >&2
    exit 1
fi
test -f "$run_desktop"
assert_resources_match \
    "$run_home/.local/share/digitalworkshop/resources" ".run install"
cmp -s "$source_resources/icons/statue.png" \
    "$run_home/.local/share/icons/hicolor/512x512/apps/digitalworkshop.png"
cmp -s "$source_resources/icons/Digital Workshop.png" \
    "$run_home/.local/share/icons/hicolor/1024x1024/apps/digitalworkshop.png"
grep -Fqx "Type=Application" "$run_desktop"
grep -Fqx "Exec=$run_binary %f" "$run_desktop"
grep -Fqx "Icon=digitalworkshop" "$run_desktop"
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$run_desktop"
fi

xvfb-run -a -s '-screen 0 1366x768x24 -dpi 96' \
    "$script_dir/smoke-installed-app.sh" "$run_binary" "$version" \
    "$evidence_dir/run-startup.log"
xvfb-run -a -s '-screen 0 1366x768x24 -dpi 96' \
    "$script_dir/smoke-settings-app.sh" "$run_settings" \
    "$evidence_dir/run-settings-startup.log"

run_cam_engine_dir="$run_home/.local/share/digitalworkshop/resources/cam-engine"
if [[ ! -d "$run_cam_engine_dir" ]]; then
    echo ".run install is missing cam-engine payload: $run_cam_engine_dir" >&2
    exit 1
fi
DW_SMOKE_JOBSPEC="$repo_root/cambridge/bridge/examples/dome-fluidnc.json" \
    "$script_dir/../smoke-cam-engine.sh" "$run_cam_engine_dir" \
    | tee "$evidence_dir/run-cam-engine-smoke.log"

HOME="$run_home" "$run_uninstaller" >"$evidence_dir/run-uninstall.log" 2>&1
for removed in \
    "$run_binary" \
    "$run_settings" \
    "$run_home/.local/bin/graphqlite.so" \
    "$run_home/.local/share/digitalworkshop" \
    "$run_desktop" \
    "$run_home/.local/share/icons/hicolor/512x512/apps/digitalworkshop.png" \
    "$run_home/.local/share/icons/hicolor/1024x1024/apps/digitalworkshop.png"; do
    if [[ -e "$removed" ]]; then
        echo "Uninstaller left an app-owned path behind: $removed" >&2
        exit 1
    fi
done

{
    echo "Digital Workshop $version Linux package smoke"
    echo "TGZ_SHA256=$(sha256sum "$tgz" | awk '{print $1}')"
    echo "RUN_SHA256=$(sha256sum "$run" | awk '{print $1}')"
    echo "TGZ_STARTUP=PASS"
    echo "TGZ_SETTINGS_STARTUP=PASS"
    echo "RUN_INSTALL=PASS"
    echo "RUN_STARTUP=PASS"
    echo "RUN_SETTINGS_STARTUP=PASS"
    echo "CAM_ENGINE_SMOKE=PASS"
    echo "RUN_UNINSTALL=PASS"
    echo "GRAPHQLITE=PASS"
    echo "FRESH_SCHEMA=PASS"
    echo "DESKTOP_ENTRY=PASS"
    echo "BUNDLED_MATERIALS=PASS"
} | tee "$evidence_dir/summary.txt"
