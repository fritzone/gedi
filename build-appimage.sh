#!/bin/bash
set -e

# --- Configuration ---
APP_NAME="gedi"
BUILD_DIR="build-appimage"
APP_DIR="AppDir"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"

echo "Creating AppImage for $APP_NAME..."

# 1. Clean up previous builds
rm -rf "$BUILD_DIR" "$APP_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$APP_DIR"

# 2. Build the project
echo "Building project..."
cd "$BUILD_DIR"
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make DESTDIR="../$APP_DIR" install
cd ..

# 3. Download linuxdeploy if not present
if [ ! -f ./linuxdeploy-x86_64.AppImage ]; then
    echo "Downloading linuxdeploy..."
    wget -c "$LINUXDEPLOY_URL"
    chmod +x linuxdeploy-x86_64.AppImage
fi

# 4. Create the AppImage
echo "Packaging AppImage..."

# We need to set some environment variables for linuxdeploy
export OUTPUT="${APP_NAME}-x86_64.AppImage"

./linuxdeploy-x86_64.AppImage --appdir "$APP_DIR" \
    -d gedi.desktop \
    -i gedi.svg \
    --output appimage

echo "AppImage created: $OUTPUT"
