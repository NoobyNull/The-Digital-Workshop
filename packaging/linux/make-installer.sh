#!/usr/bin/env bash
# Build a self-extracting .run installer using makeself
# Usage: ./make-installer.sh <build-dir> <version>
set -euo pipefail

BUILD_DIR=$(realpath "${1:?Usage: $0 <build-dir> <version>}")
VERSION="${2:?Usage: $0 <build-dir> <version>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESOURCE_SOURCE=$(realpath "$SCRIPT_DIR/../../resources")
STAGING_DIR="$(mktemp -d)"
OUTPUT="$BUILD_DIR/DigitalWorkshop-${VERSION}-linux.run"

trap 'rm -rf "$STAGING_DIR"' EXIT

require_file() {
    if [[ ! -s "$1" ]]; then
        echo "Required release payload is missing or empty: $1" >&2
        exit 1
    fi
}

require_file "$BUILD_DIR/digital_workshop"
require_file "$BUILD_DIR/dw_settings"
require_file "$BUILD_DIR/graphqlite.so"
require_file "$RESOURCE_SOURCE/icons/statue.png"
require_file "$RESOURCE_SOURCE/icons/Digital Workshop.png"
if [[ ! -x "$BUILD_DIR/digital_workshop" || ! -x "$BUILD_DIR/dw_settings" ]]; then
    echo "Release application binaries must be executable" >&2
    exit 1
fi

if ! find "$RESOURCE_SOURCE/materials" -maxdepth 1 -type f -name '*.dwmat' \
        -print -quit | grep -q .; then
    echo "Required bundled materials are missing: $RESOURCE_SOURCE/materials" >&2
    exit 1
fi
if find "$RESOURCE_SOURCE/materials" -maxdepth 1 -type f -name '*.dwmat' \
        -empty -print -quit | grep -q .; then
    echo "A bundled material is empty: $RESOURCE_SOURCE/materials" >&2
    exit 1
fi
if ! command -v makeself >/dev/null 2>&1; then
    echo "makeself is required to build the Linux installer" >&2
    exit 1
fi

# Stage only validated build outputs and canonical source resources. Never use
# build/resources here: copy_directory does not remove deleted source files, so
# that directory may contain stale release payloads.
mkdir -p "$STAGING_DIR/bin"
install -m 755 "$BUILD_DIR/digital_workshop" "$STAGING_DIR/bin/digital_workshop"
install -m 755 "$BUILD_DIR/dw_settings" "$STAGING_DIR/bin/dw_settings"
install -m 644 "$BUILD_DIR/graphqlite.so" "$STAGING_DIR/bin/graphqlite.so"
install -m 755 "$SCRIPT_DIR/install.sh" "$STAGING_DIR/install.sh"
install -m 755 "$SCRIPT_DIR/uninstall.sh" "$STAGING_DIR/uninstall.sh"
mkdir -p "$STAGING_DIR/resources"
cp -a "$RESOURCE_SOURCE/icons" "$STAGING_DIR/resources/icons"
cp -a "$RESOURCE_SOURCE/materials" "$STAGING_DIR/resources/materials"

# Build self-extracting archive
rm -f "$OUTPUT"
makeself --gzip "$STAGING_DIR" "$OUTPUT" \
    "Digital Workshop ${VERSION} Installer" \
    ./install.sh

echo ""
echo "Installer created: $OUTPUT"
