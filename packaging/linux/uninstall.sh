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
        echo "System uninstall requires root. Run with: sudo ./uninstall.sh --system"
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

echo "Uninstalling $APP_NAME from $PREFIX..."

# Remove binaries
rm -f "$BIN_DIR/$BIN_NAME"
rm -f "$BIN_DIR/$SETTINGS_BIN"
rm -f "$BIN_DIR/$GRAPHQLITE_LIB"
rm -rf "$RESOURCE_DIR"

# Remove desktop entry
rm -f "$DESKTOP_DIR/$DESKTOP_ID.desktop"

# Remove icons
rm -f "$ICON_DIR/512x512/apps/$DESKTOP_ID.png"
rm -f "$ICON_DIR/1024x1024/apps/$DESKTOP_ID.png"

# Remove the persistent uninstaller last. Linux permits a running script to
# unlink itself; rmdir only removes the app directory when no user files remain.
rm -f "$UNINSTALL_PATH"
rmdir "$APP_DIR" 2>/dev/null || true

# Update desktop database if available
if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache > /dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
fi

echo "$APP_NAME uninstalled successfully."
