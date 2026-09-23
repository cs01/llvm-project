#!/bin/bash
# Builds clang.wasm + clang.js for the playground with a stock Emscripten build
# of this tree (no source patches). CI runs this same script
# (.github/workflows/build-playground.yml). Needs emsdk activated (emcmake,
# emcc, em-config on PATH), cmake and ninja.
#
# Output: $BUILD_DIR/bin/clang.{wasm,js} (default build-wasm/ at the repo root),
# also copied next to this script, where serve.py and playground.js load them.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-wasm}"
SYSROOT="${SYSROOT:-$ROOT/playground-sysroot}"

# Headers embedded into the wasm virtual filesystem: Emscripten's libc and
# libc++ headers, plus the clang resource headers from this tree.
mkdir -p "$SYSROOT/include"
EMSDK_SYSROOT="$(em-config CACHE)/sysroot/include"
if [ ! -d "$EMSDK_SYSROOT" ]; then
  # First emcc run populates the Emscripten cache.
  echo "int main(){}" > "$BUILD_DIR.hello.c"
  emcc "$BUILD_DIR.hello.c" -o "$BUILD_DIR.hello.js"
  rm -f "$BUILD_DIR".hello.*
fi
cp -r "$EMSDK_SYSROOT"/* "$SYSROOT/include/"

CLANG_VER=$(sed -n 's/.*set(LLVM_VERSION_MAJOR \([0-9]*\).*/\1/p' \
  "$ROOT/cmake/Modules/LLVMVersion.cmake")
RESOURCE_DST="$SYSROOT/lib/clang/$CLANG_VER/include"
mkdir -p "$RESOURCE_DST"
# Only the portable C/C++ headers; the architecture intrinsics (avx*, arm*,
# amx*, ...) would add ~14MB to the embedded filesystem.
for pattern in \
  '__stddef*' '__stdarg*' '__stdc*' \
  'stddef.h' 'stdarg.h' 'stdbool.h' 'stdnoreturn.h' 'stdatomic.h' \
  'stdalign.h' 'stdint.h' \
  'limits.h' 'float.h' 'inttypes.h' 'iso646.h' \
  '__clang_hip_*.h' '__wasm*.h' \
  'unwind.h' 'tgmath.h' 'varargs.h'; do
  for h in "$ROOT"/clang/lib/Headers/$pattern; do
    [ -f "$h" ] && cp "$h" "$RESOURCE_DST/"
  done
done
echo "Embedded sysroot: $(du -sh "$SYSROOT" | cut -f1)"

# host_path@virtual_path: resource headers at /lib/clang/<ver>/include
# (-resource-dir), libc and libc++ at /include (-isystem).
EMBED_FLAGS="--embed-file $RESOURCE_DST@/lib/clang/$CLANG_VER/include"
EMBED_FLAGS+=" --embed-file $SYSROOT/include@/include"

LAUNCHER=()
if command -v ccache >/dev/null; then
  LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER=ccache
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

emcmake cmake -S "$ROOT/llvm" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=WebAssembly \
  -DLLVM_ENABLE_THREADS=OFF \
  -DLLVM_ENABLE_EH=OFF \
  -DLLVM_ENABLE_RTTI=OFF \
  -DLLVM_ENABLE_PIC=OFF \
  -DLLVM_BUILD_TOOLS=OFF \
  -DLLVM_BUILD_UTILS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF \
  "${LAUNCHER[@]}" \
  -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=callMain -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=33554432 -sINITIAL_MEMORY=268435456 $EMBED_FLAGS"
ninja -C "$BUILD_DIR" clang
cp "$BUILD_DIR/bin/clang.wasm" "$BUILD_DIR/bin/clang.js" "$ROOT/playground/"
ls -lh "$BUILD_DIR/bin/clang.wasm" "$BUILD_DIR/bin/clang.js"
