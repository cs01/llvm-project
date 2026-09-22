# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

Lit tests for Nullability Safety:
- `test/SemaCXX/nullability-safety-analysis.cpp` - core analysis: narrowing, dereference, aliases, control flow (~2000 lines)
- `test/SemaCXX/nullability-safety-cxx-features.cpp` - templates, lambdas, coroutines, smart pointers, structured bindings
- `test/SemaCXX/nullability-safety-adoption.cpp` - gradual adoption, false-positive suppression, perf stress
- `test/SemaCXX/nullability-safety-crubit-regression.cpp` - regression tests ported from Crubit
- `test/SemaCXX/nullability-safety-warning-groups.cpp` - warning group suppression/promotion
- `test/SemaCXX/nullability-safety-default-nonnull.cpp` - `-fnullability-default=nonnull` mode
- `test/SemaCXX/nullability-safety-real-smartptr.cpp` - real stdlib smart pointer tests (requires system headers)
- `test/Sema/nullability-safety-c.c` - all C-mode tests: narrowing, idioms, call invalidation
- `test/Driver/nullability-safety-flags.c` - driver flag forwarding

Run all Nullability Safety tests:
```bash
build/bin/llvm-lit -v clang/test/SemaCXX/nullability-safety-*.cpp clang/test/Sema/nullability-safety-*.c clang/test/Driver/nullability-safety-flags.c
```

## Key Custom Flags

- `-fnullability-safety` - enables flow-sensitive nullability analysis
- `-fnullability-default=nullable|nonnull|unspecified` - sets default nullability for unannotated pointers

## Key Files

- `lib/Analysis/NullabilitySafety.cpp` - CFG-based forward dataflow analysis: nullability narrowing, dereference checking, condition analysis, per-edge state tracking
- `include/clang/Analysis/Analyses/NullabilitySafety.h` - analysis interface: `NullabilitySafetyHandler` callback, `runNullabilitySafetyAnalysis` entry point
- `lib/Sema/AnalysisBasedWarnings.cpp` - wires the analysis into Clang's warning pipeline: `NullabilitySafetyReporter`, CFG build options
- `lib/Sema/SemaDecl.cpp` - `warn_nullability_safety_null_init` diagnostic for null-init of _Nonnull vars
- `include/clang/Sema/Sema.h` - `functionHasNullabilityAnnotations` helper
- `lib/Sema/Sema.cpp` - `functionHasNullabilityAnnotations`, `diagnoseNullableToNonnullConversion`
- `lib/Driver/ToolChains/Clang.cpp` - driver-to-cc1 flag forwarding
- `include/clang/Options/Options.td` - flag definitions
- `include/clang/Basic/DiagnosticSemaKinds.td` - `warn_nullability_safety_dereference` diagnostic
- `include/clang/Basic/DiagnosticGroups.td` - `NullabilitySafetyDereference` / `NullabilitySafety` diagnostic groups

## Architecture

The analysis follows the same pattern as Clang's ThreadSafety and UninitializedValues analyses: a standalone analysis in `lib/Analysis/` invoked from `AnalysisBasedWarnings.cpp`, reporting results via a handler interface.

### Three-layer design

**`lib/Analysis/NullabilitySafety.cpp`** — the analysis algorithm. Operates on the CFG (control flow graph), which Clang builds automatically from the AST. Uses `ForwardDataflowWorklist` for fixpoint iteration over CFG blocks in reverse-post-order. Tracks `NullState` (sets of narrowed variables and members) per edge, intersecting at merge points. Reports dereferences of nullable pointers via `NullabilitySafetyHandler` callbacks.

**`lib/Sema/AnalysisBasedWarnings.cpp`** — the glue layer. Builds the CFG, instantiates the analysis, and converts handler callbacks into `S.Diag()` calls. Gated by `EnableNullabilitySafety` (precomputed from LangOpts and diagnostic state).

**`test/SemaCXX/nullability-safety-*.cpp`** — C++ tests in `test/SemaCXX/`, C tests in `test/Sema/`. This matches ThreadSafety's test layout. Tests are consolidated into a few large files rather than many small ones.

### Dataflow analysis details

Per-edge state tracking: `EdgeStates[{PredBlockID, SuccBlockID}]` stores the narrowing state along each CFG edge. This lets branch-refined narrowing (e.g., true vs false branch of `if (p)`) propagate correctly. Entry state for each block is computed by intersecting edge states from all predecessors — narrowed only if ALL paths agree.

`getTerminalCondition()` extracts the actual sub-expression being tested in each CFG block. The CFG decomposes `&&`/`||` into separate blocks, but the terminator expression is the full `p && q`. This helper recursively follows the RHS of `&&`/`||` to find the leaf that's actually being evaluated in that block.

Two narrowing sets in `NullState`:
- `NarrowedVars` — `DenseSet<const VarDecl*>` for local variables and parameters
- `NarrowedMembers` — `DenseSet<MemberAccessPath>` for member accesses at any depth (`var.field`, `this->field`, `o.inner.x`). A `MemberAccessPath` is a root `VarDecl*` (nullptr for `this->`) plus a `SmallVector<const FieldDecl*>` chain of fields, compared element-wise. `decomposeMemberAccess()` walks any `MemberExpr` chain to build one.

Transfer functions handle: `DeclStmt` (nonnull init, alias tracking, smart pointer narrowing), `BinaryOperator` (assignment invalidation, pointer arithmetic on nullable, pointer diff), `UnaryOperator` (`*` deref check, `++`/`--` arithmetic check and invalidation), `MemberExpr` (`->` deref check, smart pointer `operator->` check), `ArraySubscriptExpr` (subscript deref check), `CallExpr` (`__builtin_assume` narrowing, argument checking), `CXXConstructExpr` (constructor argument checking), `InitListExpr` (aggregate init of nonnull fields), `ReturnStmt` (nullable return from nonnull function, all-returns-nonnull inference).

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
