# AGENTS.md

# Clang Nullability Safety Fork

This is a fork of LLVM/Clang that adds compile-time null pointer dereference checking via flow-sensitive analysis. The fork lives on branch `nullability-safety`.

## Build

```bash
cd /data/users/cssmith/git/llvm-nullsafe
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_PARALLEL_LINK_JOBS=8 \
  -DLLVM_ENABLE_ASSERTIONS=ON
ninja -C build -j72 clang clangd
```

Built compiler: `build/bin/clang`
Built language server: `build/bin/clangd`

## Testing

```bash
ninja -C build check-clang-unit
```

Run all Nullability Safety tests:
```bash
build/bin/llvm-lit -v clang/test/SemaCXX/nullability-safety-*.cpp clang/test/Sema/nullability-safety-*.c clang/test/Driver/nullability-safety-flags.c
```

## Key Custom Flags

- `-fnullability-safety` - enables flow-sensitive nullability analysis
- `-fnullability-default=nullable|nonnull|unspecified` - sets default nullability for unannotated pointers

## Architecture

The analysis follows the same pattern as Clang's ThreadSafety and UninitializedValues analyses: a standalone analysis in `lib/Analysis/` invoked from `AnalysisBasedWarnings.cpp`, reporting results via a handler interface.

### Dataflow analysis details

`getTerminalCondition()` extracts the actual sub-expression being tested in each CFG block. The CFG decomposes `&&`/`||` into separate blocks, but the terminator expression is the full `p && q`. This helper recursively follows the RHS of `&&`/`||` to find the leaf that's actually being evaluated in that block.

### Gradual adoption

Flow-sensitive checking only activates per-function when inside a `#pragma clang assume_nonnull` region, when `-fnullability-default` is set to something other than `unspecified`, or when the function has explicit nullability annotations on its parameters or return type. This is computed locally in `AnalysisBasedWarnings.cpp:IssueWarnings` (not stored on Sema) to avoid scoping bugs when lambda bodies interleave with enclosing function processing.

### Design decisions

- `this->x` and `*this` dereferences are suppressed (`this` is never null in well-defined C++)
- Smart pointer `operator->` is checked with smart-pointer-aware narrowing (warns if not narrowed)
- Non-smart-pointer overloaded `operator->` (iterators, etc.) is skipped
- Function calls do NOT invalidate narrowing — a pragmatic choice to avoid excessive noise, matching the approach of ThreadSafety
- Analysis is intraprocedural — it does not look inside called functions to determine nullability

## Branch Management

Two branches are maintained:

- **`nullability-safety`** — the full fork with playground, install scripts, CI, WASM build, docs, etc. **All development happens here. Always work on this branch.**
- **`nullsafe-upstream`** — clean branch with only the core compiler changes, used for the upstream PR to `llvm/llvm-project`. **Never work directly on this branch** — it is rebuilt from `nullability-safety` via `sync-upstream.sh`.

Run `./tools/sync-upstream.sh` to rebuild `nullsafe-upstream` from the current state of `nullability-safety`. It filters out all fork-only files and creates a single commit on top of `llvm/main`.

**When adding new files:** `tools/sync-upstream.sh` is an allowlist. Compiler files belong under one of its `INCLUDE_PREFIXES`; a fork-only file that lands under an included prefix must be added to `EXCLUDE_PATHS` so it doesn't leak into the upstream branch.

## Conventions

- This is a compiler — correctness matters above all. Every change should have a lit test.
- Use `// expected-warning` and `// expected-error` in lit tests per clang convention.
- Diagnostic messages go in `DiagnosticSemaKinds.td`, referenced via `diag::warn_*` enums.
- When the compiler prints pointer types, `_Nullable` may appear in the printed type (e.g., `'int * _Nullable'`). Account for this in test expected-warning strings.
