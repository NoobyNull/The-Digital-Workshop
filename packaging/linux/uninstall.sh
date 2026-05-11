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
        echo "System uninstall requires root. Run with: sudo ./uninstall.sh --system"
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

echo "Uninstalling $APP_NAME from $PREFIX..."

# Remove binaries
rm -f "$BIN_DIR/$BIN_NAME"
rm -f "$BIN_DIR/$SETTINGS_BIN"
rm -rf "$RESOURCE_DIR"

# Remove desktop entry
rm -f "$DESKTOP_DIR/$DESKTOP_ID.desktop"

# Remove icons
rm -f "$ICON_DIR/512x512/apps/$DESKTOP_ID.png"
rm -f "$ICON_DIR/1024x1024/apps/$DESKTOP_ID.png"

# Update desktop database if available
if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache > /dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
fi

echo "$APP_NAME uninstalled successfully."
