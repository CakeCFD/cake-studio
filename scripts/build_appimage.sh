#!/bin/bash
# Build CakeCFD AppImage
# Usage: ./scripts/build_appimage.sh [--release]
# Requires: cmake build already done, or pass --build to rebuild first

set -e
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-}"
BINARY="build/src/gui/CFDStudio"
TOOLS_DIR=".appimage-tools"
APPDIR="AppDir"
ICON_SRC="packaging/cakecfd.svg"
ICON_PNG="packaging/cakecfd.png"

# Optionally rebuild
if [ "$BUILD_TYPE" = "--build" ]; then
    echo "Building CakeCFD..."
    cmake --build build --target CFDStudio -j"$(nproc)"
fi

if [ ! -f "$BINARY" ]; then
    echo "Error: $BINARY not found. Run cmake build first, or pass --build."
    exit 1
fi

# Convert SVG icon to PNG (256x256)
if [ ! -f "$ICON_PNG" ]; then
    echo "Converting icon..."
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w 256 -h 256 "$ICON_SRC" -o "$ICON_PNG"
    elif command -v inkscape &>/dev/null; then
        inkscape --export-png="$ICON_PNG" -w 256 -h 256 "$ICON_SRC" 2>/dev/null
    elif command -v convert &>/dev/null; then
        convert -background none -resize 256x256 "$ICON_SRC" "$ICON_PNG"
    else
        echo "Error: need rsvg-convert, inkscape, or imagemagick to convert icon."
        echo "  sudo apt install librsvg2-bin"
        exit 1
    fi
fi

# Download linuxdeploy tools
mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

if [ ! -f "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    wget -q --show-progress -O "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

if [ ! -f "$LINUXDEPLOY_QT" ]; then
    echo "Downloading linuxdeploy Qt plugin..."
    wget -q --show-progress -O "$LINUXDEPLOY_QT" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "$LINUXDEPLOY_QT"
fi

# Clean previous AppDir
rm -rf "$APPDIR"

# Bundle binary + Qt + all shared libs
echo "Bundling application..."
export QMAKE="${QMAKE:-qmake}"
export PATH="$TOOLS_DIR:$PATH"

"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$BINARY" \
    --desktop-file "packaging/cakecfd.desktop" \
    --icon-file "$ICON_PNG" \
    --plugin qt \
    --output appimage

# Replace generated AppRun with ours (sources OpenFOAM before launch)
echo "Installing custom AppRun..."
cp packaging/AppRun "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

# Re-package with appimagetool (linuxdeploy already ran it, so this just re-wraps)
# The AppImage is already output by linuxdeploy above as CakeCFD-x86_64.AppImage
APPIMAGE=$(ls CakeCFD-*.AppImage 2>/dev/null | head -1)
if [ -n "$APPIMAGE" ]; then
    echo ""
    echo "Done: $APPIMAGE"
    echo "Run: chmod +x $APPIMAGE && ./$APPIMAGE"
else
    echo "AppImage build failed - check output above."
    exit 1
fi
