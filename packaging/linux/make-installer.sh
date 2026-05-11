#!/bin/bash
# Build a self-extracting .run installer using makeself
# Usage: ./make-installer.sh <build-dir> <version>
set -e

BUILD_DIR="${1:?Usage: $0 <build-dir> <version>}"
VERSION="${2:?Usage: $0 <build-dir> <version>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STAGING_DIR="$(mktemp -d)"
OUTPUT="$BUILD_DIR/DigitalWorkshop-${VERSION}-linux.run"

trap 'rm -rf "$STAGING_DIR"' EXIT

# Stage files
mkdir -p "$STAGING_DIR/bin"
cp "$BUILD_DIR/digital_workshop" "$STAGING_DIR/bin/"
cp "$BUILD_DIR/dw_settings" "$STAGING_DIR/bin/"
cp "$SCRIPT_DIR/install.sh" "$STAGING_DIR/"
cp "$SCRIPT_DIR/uninstall.sh" "$STAGING_DIR/"
if [ -d "$BUILD_DIR/resources" ]; then
    cp -R "$BUILD_DIR/resources" "$STAGING_DIR/"
else
    cp -R "$SCRIPT_DIR/../../resources" "$STAGING_DIR/"
fi

# Build self-extracting archive
makeself --gzip "$STAGING_DIR" "$OUTPUT" \
    "Digital Workshop ${VERSION} Installer" \
    ./install.sh

echo ""
echo "Installer created: $OUTPUT"
