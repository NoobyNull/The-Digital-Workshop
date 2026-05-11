#!/bin/bash
set -e

APP_NAME="Digital Workshop"
BIN_NAME="digital_workshop"
SETTINGS_BIN="dw_settings"
DESKTOP_ID="digitalworkshop"

# Default to user install, --system for /usr/local
PREFIX="$HOME/.local"
if [ "$1" = "--system" ]; then
    if [ "$(id -u)" -ne 0 ]; then
        echo "System install requires root. Run with: sudo ./install.sh --system"
        exit 1
    fi
    PREFIX="/usr/local"
fi

BIN_DIR="$PREFIX/bin"
RESOURCE_DIR="$PREFIX/share/digitalworkshop/resources"
DESKTOP_DIR="$HOME/.local/share/applications"
if [ "$1" = "--system" ]; then
    DESKTOP_DIR="/usr/share/applications"
fi

ICON_DIR="$HOME/.local/share/icons/hicolor"
if [ "$1" = "--system" ]; then
    ICON_DIR="/usr/share/icons/hicolor"
fi

echo "Installing $APP_NAME to $PREFIX..."

# Install binaries
mkdir -p "$BIN_DIR"
install -m 755 bin/$BIN_NAME "$BIN_DIR/$BIN_NAME"
install -m 755 bin/$SETTINGS_BIN "$BIN_DIR/$SETTINGS_BIN"

# Install app-owned resources. Runtime also supports this prefix/share layout,
# so no host-level symlinks or environment changes are required.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESOURCE_SRC="$SCRIPT_DIR/resources"
if [ -d "$RESOURCE_SRC" ]; then
    rm -rf "$RESOURCE_DIR"
    mkdir -p "$(dirname "$RESOURCE_DIR")"
    cp -R "$RESOURCE_SRC" "$RESOURCE_DIR"
fi

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
echo "  Uninstall: $(dirname "$0")/uninstall.sh"
if [ "$1" != "--system" ]; then
    echo ""
    echo "Make sure $BIN_DIR is in your PATH."
fi
