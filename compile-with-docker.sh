#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------
# F4HWN Fusion Firmware Build Script
# ---------------------------------------------
# This script builds the Fusion-only firmware
# using Docker for consistent toolchain environment.
#
# Usage:
#   ./compile-with-docker.sh        # Build firmware
#   ./compile-with-docker.sh lint   # Run linter (if configured)
#   ./compile-with-docker.sh format # Format code (if configured)
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
COMMAND=${1:-build}

# ---------------------------------------------
# Build the Docker image (only needed once)
# ---------------------------------------------
echo "🐳 Building Docker image..."
docker build -t "$IMAGE" .

# ---------------------------------------------
# Clean existing CMake cache
# ---------------------------------------------
rm -rf build

# ---------------------------------------------
# Build function
# ---------------------------------------------
build_firmware() {
    echo ""
    echo "=== 🚀 Building Fusion Firmware ==="
    echo "---------------------------------------------"
    
    # Show compiler info
    docker run --rm -v "$PWD":/src -w /src "$IMAGE" \
        bash -c "/opt/toolchain/bin/arm-none-eabi-gcc --version | head -n1"
    
    echo ""
    echo "Preset CMake variables:"
    echo ""
    echo "  CMAKE_BUILD_TYPE=\"Release\""
    echo "  CMAKE_TOOLCHAIN_FILE:FILEPATH=\"/src/cmake/gcc-arm-none-eabi.cmake\""
    echo "  EDITION_STRING=\"Fusion\""
    echo "  TARGET=\"f4hwn.fusion\""
    echo "  VERSION_STRING_1=\"v0.22\""
    echo "  VERSION_STRING_2=\"v4.3.2\""
    echo ""
    
    # Configure and build
    docker run --rm -v "$PWD":/src -w /src "$IMAGE" \
        bash -c "cmake --preset Fusion && cmake --build build"
    
    echo ""
    echo "✅ Build complete!"
    echo "📦 Firmware files in: build/"
    echo "   - f4hwn.fusion.bin (flash this file)"
    echo "   - f4hwn.fusion.elf"
    echo "   - f4hwn.fusion.hex"
}

# ---------------------------------------------
# Main
# ---------------------------------------------
case "$COMMAND" in
    build|"")
        build_firmware
        ;;
    lint)
        echo "ℹ️  Lint support not configured yet"
        exit 1
        ;;
    format)
        echo ""
        echo "=== 🎨 Formatting Code with clang-format ==="
        echo "---------------------------------------------"
        
        docker run --rm -v "$PWD":/src -w /src "$IMAGE" \
            bash -c "find App -path 'App/external' -prune -o \( -name '*.c' -o -name '*.h' \) -print | xargs clang-format -i --style=file"
        
        echo ""
        echo "✅ Code formatting complete!"
        echo "All C/H files in App/ have been formatted"
        ;;
    *)
        echo "❌ Unknown command: '$COMMAND'"
        echo "Valid commands: build, lint, format"
        exit 1
        ;;
esac
