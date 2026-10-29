# Trying the build speedups

Three independent changes, described in `RFC-incremental-builds.md`:

1. AppleClang PCH fix (`llvm/cmake/modules/HandleLLVMOptions.cmake`): automatic
   with AppleClang, nothing to configure.
2. `-fcodegen-cache-path=<dir>`: reuse objects when a TU's IR is unchanged.
3. `-fsemantic-deps` + `clang-semindex` + `semninja`: recompile only TUs that
   used what a header edit changed.

Needs `ninja` and [`bun`](https://bun.sh).

## Stage 1: build the compiler with these patches

```sh
cmake -G Ninja -S llvm -B build-stage1 -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64
ninja -C build-stage1 clang clang-semindex
```

## Stage 2: build LLVM with that compiler

```sh
S1=$PWD/build-stage1/bin
cmake -G Ninja -S llvm -B build-stage2 -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
  -DCMAKE_C_COMPILER=$S1/clang -DCMAKE_CXX_COMPILER=$S1/clang++ \
  -DCMAKE_CXX_FLAGS="-Xclang -fsemantic-deps -Xclang -fcodegen-cache-path=$PWD/build-stage2/codegen-cache"
bun clang/utils/semninja/semninja.ts -C build-stage2 --indexer $S1/clang-semindex clang
```

Then edit a header and run the same `semninja.ts` command instead of `ninja`.
Add `--verify` to recompile every kept object and compare it byte for byte.
Plain `ninja` still works on the same build dir and agrees afterwards.

## A/B on a real commit

```sh
clang/utils/semninja/demo.sh build-stage2 <commit-with-header-edits> $S1/clang-semindex
```

Reverts the commit's changes, rebuilds with semninja (verifying), re-applies
them, rebuilds with plain ninja, and prints both times.
