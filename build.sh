#!/bin/bash
# Build script for WSL — cross-compiles to Windows .exe using MinGW-w64
set -e

echo "╔══════════════════════════════════════════════════════╗"
echo "║  Building Antigravity Chrome Bridge (C++ → .exe)    ║"
echo "╚══════════════════════════════════════════════════════╝"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="build-win64"
TOOLCHAIN="$SCRIPT_DIR/mingw-toolchain.cmake"

# Configure
cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -G "Unix Makefiles"

# Build
cmake --build "$BUILD_DIR" --config Release -j$(nproc)

# Result
EXE="$BUILD_DIR/antigravity-chrome-bridge.exe"
if [ -f "$EXE" ]; then
    SIZE=$(du -h "$EXE" | cut -f1)
    echo ""
    echo "═══════════════════════════════════════════════"
    echo "  ✓ Build successful!"
    echo "  Binary: $EXE ($SIZE)"
    echo "═══════════════════════════════════════════════"
else
    echo "✗ Build failed — no binary produced."
    exit 1
fi
