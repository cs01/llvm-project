# Building Safe C

[Back to the README](../README.md)

## How it is built

- **Base:** Apple's [swiftlang/llvm-project](https://github.com/swiftlang/llvm-project)
  `next` branch, which is LLVM `main` plus Apple's patches, including `-fbounds-safety`.
- **Null safety:** the [nullability-safety](https://github.com/cs01/llvm-project/tree/nullability-safety)
  fork, applied on top. Its own docs are in [NULLSAFE.md](../NULLSAFE.md).
- **Safe C:** the `-fsafe-c` flag and the rules above.

```sh
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64
ninja -C build clang
build/bin/clang -fsafe-c -isysroot $(xcrun --show-sdk-path) example.c
```

## What `-fsafe-c` turns on

One flag; no other flags are needed:

- `-fbounds-safety` (Apple's bounds checks).
- Pointers are non-null unless marked `_Nullable`, checked by flow analysis.
- Safe C's ownership rules, checked on ClangIR.
- Clang's thread-safety analysis (`-Wthread-safety`), for `_Guarded_by`,
  `_Requires`, `_Acquires` and `_Releases`.
- `_FORTIFY_SOURCE` off: its checks are replaced by the bounds checks.
- Every memory-safety finding is an error, not a warning, and `-w` or
  `-Wno-...` cannot turn it off. Code inside `__unsafe { }` is exempt.

## Prior work

Cyclone and Checked C made C dialects; Fil-C makes C safe at run time with a garbage
collector. Safe C stays plain C, checked inside Clang: at compile time where it can be
proven, with a trap where it cannot.
