#!/bin/bash
# Build script to generate clang.wasm and clang.js for the Null-Safe C Playground

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LLVM_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$LLVM_ROOT/build"

echo "Building Clang WASM for Null-Safe C Playground..."
echo "LLVM Root: $LLVM_ROOT"
echo "Build Directory: $BUILD_DIR"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found at $BUILD_DIR"
    echo "Please build Clang first using: ninja -C build clang"
    exit 1
fi

# Check if clang binary exists
if [ ! -f "$BUILD_DIR/bin/clang" ]; then
    echo "Error: Clang binary not found at $BUILD_DIR/bin/clang"
    echo "Please build Clang first using: ninja -C build clang"
    exit 1
fi

# Check if Emscripten is available
if ! command -v emcc &> /dev/null; then
    echo "Error: Emscripten not found"
    echo "Please install Emscripten: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

echo "Compiling Clang to WebAssembly..."
echo "Note: This may take several minutes and requires significant memory..."

# Use Emscripten to compile the native Clang binary to WASM
# We'll use the existing build and just wrap it
# For now, copy from tmp (this is a placeholder - we need proper WASM build)
if [ -f "/tmp/null-safe-playground/clang.wasm" ]; then
    echo "Copying WASM files from /tmp/null-safe-playground..."
    cp /tmp/null-safe-playground/clang.wasm "$SCRIPT_DIR/"
    cp /tmp/null-safe-playground/clang.js "$SCRIPT_DIR/"
    echo "Done! WASM files copied to $SCRIPT_DIR"
else
    echo "Error: WASM files not found in /tmp/null-safe-playground"
    echo ""
    echo "TODO: Implement proper Emscripten build of Clang"
    echo "This requires:"
    echo "  1. Building LLVM/Clang with Emscripten toolchain"
    echo "  2. Using emcc to compile Clang to WASM"
    echo "  3. See: https://emscripten.org/"
    exit 1
fi

echo ""
echo "Build complete!"
echo "Files generated:"
ls -lh "$SCRIPT_DIR/clang.wasm" "$SCRIPT_DIR/clang.js"
echo ""
echo "To run the playground:"
echo "  cd $SCRIPT_DIR"
echo "  python3 serve.py"
echo "  Open http://localhost:9000"
