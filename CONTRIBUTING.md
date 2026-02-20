# Contributing to Null-Safe Clang

Development branch: `null-safe-c-dev`

## Building

### Standard (x86)

**Clang only:**
```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang" ../llvm
ninja clang
```

**Clang + clangd (language server):**
```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" ../llvm
ninja clang clangd
```

### WASM (playground)
```bash
# Clone and setup Emscripten (first time only)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
cd ..

# Build WASM
mkdir build-wasm && cd build-wasm
export PATH="/path/to/emsdk/upstream/emscripten:$PATH"
emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang" -DLLVM_TARGETS_TO_BUILD="WebAssembly" \
  -DLLVM_ENABLE_THREADS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=callMain -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1" \
  ../llvm
ninja clang

# Output: bin/clang.wasm and bin/clang.js
```

## Testing
```bash
cd build
ninja FileCheck count not split-file llvm-config
./bin/llvm-lit -v ../clang/test/Sema/strict-nullability.c
```

## Releases

**Playground:** Push to `null-safe-c-dev` → deploys GitHub Pages automatically.

**WASM files:** `git tag playground-v1.0 && git push origin playground-v1.0`

**Binaries:** `git tag v1.0.0 && git push origin v1.0.0`

Downloads: https://github.com/cs01/llvm-project/releases
