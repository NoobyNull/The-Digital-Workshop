#!/usr/bin/env bash
set -euo pipefail

APP_NAME="Digital Workshop"
BIN_NAME="digital_workshop"
SETTINGS_BIN="dw_settings"
GRAPHQLITE_LIB="graphqlite.so"
DESKTOP_ID="digitalworkshop"

# Default to user install, --system for /usr/local
MODE="${1:-}"
PREFIX="$HOME/.local"
if [[ "$MODE" == "--system" ]]; then
    if [ "$(id -u)" -ne 0 ]; then
        echo "System install requires root. Run with: sudo ./install.sh --system"
        exit 1
    fi
    PREFIX="/usr/local"
fi

BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/digitalworkshop"
RESOURCE_DIR="$APP_DIR/resources"
UNINSTALL_PATH="$APP_DIR/uninstall.sh"
DESKTOP_DIR="$HOME/.local/share/applications"
if [[ "$MODE" == "--system" ]]; then
    DESKTOP_DIR="/usr/share/applications"
fi

ICON_DIR="$HOME/.local/share/icons/hicolor"
if [[ "$MODE" == "--system" ]]; then
    ICON_DIR="/usr/share/icons/hicolor"
fi

echo "Installing $APP_NAME to $PREFIX..."

# Validate the archive before changing the installation.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESOURCE_SRC="$SCRIPT_DIR/resources"
for required in \
    "$SCRIPT_DIR/bin/$BIN_NAME" \
    "$SCRIPT_DIR/bin/$SETTINGS_BIN" \
    "$SCRIPT_DIR/bin/$GRAPHQLITE_LIB" \
    "$SCRIPT_DIR/uninstall.sh" \
    "$RESOURCE_SRC/icons/statue.png" \
    "$RESOURCE_SRC/icons/Digital Workshop.png" \
    "$RESOURCE_SRC/cam-engine/dw-cam-engine.js" \
    "$RESOURCE_SRC/cam-engine/bun"; do
    if [[ ! -s "$required" ]]; then
        echo "Installer payload is missing or empty: $required" >&2
        exit 1
    fi
done
if [[ ! -x "$RESOURCE_SRC/cam-engine/bun" ]]; then
    echo "Installer payload cam-engine bun runtime is not executable" >&2
    exit 1
fi
if ! find "$RESOURCE_SRC/materials" -maxdepth 1 -type f -name '*.dwmat' \
        -print -quit | grep -q .; then
    echo "Installer payload has no bundled materials" >&2
    exit 1
fi
if find "$RESOURCE_SRC/materials" -maxdepth 1 -type f -name '*.dwmat' \
        -empty -print -quit | grep -q .; then
    echo "Installer payload contains an empty bundled material" >&2
    exit 1
fi

# Install binaries
mkdir -p "$BIN_DIR"
install -m 755 "$SCRIPT_DIR/bin/$BIN_NAME" "$BIN_DIR/$BIN_NAME"
install -m 755 "$SCRIPT_DIR/bin/$SETTINGS_BIN" "$BIN_DIR/$SETTINGS_BIN"
install -m 644 "$SCRIPT_DIR/bin/$GRAPHQLITE_LIB" "$BIN_DIR/$GRAPHQLITE_LIB"

# Install app-owned resources. Runtime also supports this prefix/share layout,
# so no host-level symlinks or environment changes are required.
rm -rf "$RESOURCE_DIR"
mkdir -p "$APP_DIR"
cp -R "$RESOURCE_SRC" "$RESOURCE_DIR"
install -m 755 "$SCRIPT_DIR/uninstall.sh" "$UNINSTALL_PATH"

# Install icons
ICON_SRC="$SCRIPT_DIR/resources/icons"
if [ ! -d "$ICON_SRC" ]; then
    ICON_SRC="$(dirname "$SCRIPT_DIR")/resources/icons"
fi

if [ -f "$ICON_SRC/statue.png" ]; then
    mkdir -p "$ICON_DIR/512x512/apps"
    install -m 644 "$ICON_SRC/statue.png" "$ICON_DIR/512x512/apps/$DESKTOP_ID.png"
fi
if [ -f "$ICON_SRC/Digital Workshop.png" ]; then
    mkdir -p "$ICON_DIR/1024x1024/apps"
    install -m 644 "$ICON_SRC/Digital Workshop.png" "$ICON_DIR/1024x1024/apps/$DESKTOP_ID.png"
fi

# Install desktop entry
mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_DIR/$DESKTOP_ID.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=$APP_NAME
Comment=3D model management, G-code analysis, and 2D cut optimization
Exec=$BIN_DIR/$BIN_NAME %f
Icon=$DESKTOP_ID
Terminal=false
Categories=Graphics;3DGraphics;Engineering;
MimeType=model/stl;model/obj;application/vnd.ms-package.3dmanufacturing-3dmodel+xml;
DESKTOP

chmod 644 "$DESKTOP_DIR/$DESKTOP_ID.desktop"

# Update desktop database if available
if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi

echo ""
echo "$APP_NAME installed successfully."
echo "  Binaries:  $BIN_DIR/$BIN_NAME"
echo "             $BIN_DIR/$SETTINGS_BIN"
echo "  Desktop:   $DESKTOP_DIR/$DESKTOP_ID.desktop"
echo "  CAM engine: $RESOURCE_DIR/cam-engine"
if [[ "$MODE" == "--system" ]]; then
    echo "  Uninstall: sudo $UNINSTALL_PATH --system"
else
    echo "  Uninstall: $UNINSTALL_PATH"
    echo ""
    echo "Make sure $BIN_DIR is in your PATH."
fi
