# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Nullsafe Clang Fork

This is a fork of LLVM/Clang that adds compile-time null pointer dereference checking via flow-sensitive analysis. The fork lives on branch `nullsafe-clang-dev`.

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

Lit tests for nullsafe features:
- `test/SemaCXX/flow-nullability-analysis.cpp` - core analysis: narrowing, dereference, aliases, control flow (~2000 lines)
- `test/SemaCXX/flow-nullability-cxx-features.cpp` - templates, lambdas, coroutines, smart pointers, structured bindings
- `test/SemaCXX/flow-nullability-adoption.cpp` - gradual adoption, false-positive suppression, perf stress
- `test/SemaCXX/flow-nullability-crubit-regression.cpp` - regression tests ported from Crubit
- `test/SemaCXX/flow-nullability-warning-groups.cpp` - warning group suppression/promotion
- `test/SemaCXX/flow-nullability-default-nonnull.cpp` - `-fnullability-default=nonnull` mode
- `test/SemaCXX/flow-nullability-real-smartptr.cpp` - real stdlib smart pointer tests (requires system headers)
- `test/Sema/flow-nullability-c.c` - all C-mode tests: narrowing, idioms, call invalidation
- `test/Driver/nullsafe-flags.c` - driver flag forwarding

Run all nullsafe tests:
```bash
build/bin/llvm-lit -v clang/test/SemaCXX/flow-nullability-*.cpp clang/test/Sema/flow-nullability-*.c clang/test/Driver/nullsafe-flags.c
```

## Key Custom Flags

- `-fflow-sensitive-nullability` - enables flow-sensitive nullability analysis
- `-fnullability-default=nullable|nonnull|unspecified` - sets default nullability for unannotated pointers

## Key Files (Nullsafe Changes)

- `lib/Analysis/FlowNullability.cpp` - CFG-based forward dataflow analysis: nullability narrowing, dereference checking, condition analysis, per-edge state tracking
- `include/clang/Analysis/Analyses/FlowNullability.h` - analysis interface: `FlowNullabilityHandler` callback, `runFlowNullabilityAnalysis` entry point
- `lib/Sema/AnalysisBasedWarnings.cpp` - wires the analysis into Clang's warning pipeline: `FlowNullabilityReporter`, CFG build options
- `lib/Sema/SemaDecl.cpp` - `warn_null_init_nonnull` diagnostic for null-init of _Nonnull vars
- `include/clang/Sema/Sema.h` - `functionHasNullabilityAnnotations` helper
- `lib/Sema/Sema.cpp` - `functionHasNullabilityAnnotations`, `diagnoseNullableToNonnullConversion`
- `lib/Driver/ToolChains/Clang.cpp` - driver-to-cc1 flag forwarding
- `include/clang/Driver/Options.td` - flag definitions
- `include/clang/Basic/DiagnosticSemaKinds.td` - `warn_flow_nullable_dereference` diagnostic
- `include/clang/Basic/DiagnosticGroups.td` - `FlowNullableDereference` / `FlowNullability` diagnostic groups

## Architecture

The analysis follows the same pattern as Clang's ThreadSafety and UninitializedValues analyses: a standalone analysis in `lib/Analysis/` invoked from `AnalysisBasedWarnings.cpp`, reporting results via a handler interface.

### Three-layer design

**`lib/Analysis/FlowNullability.cpp`** — the analysis algorithm. Operates on the CFG (control flow graph), which Clang builds automatically from the AST. Uses `ForwardDataflowWorklist` for fixpoint iteration over CFG blocks in reverse-post-order. Tracks `NullState` (sets of narrowed variables and members) per edge, intersecting at merge points. Reports dereferences of nullable pointers via `FlowNullabilityHandler` callbacks.

**`lib/Sema/AnalysisBasedWarnings.cpp`** — the glue layer. Builds the CFG, instantiates the analysis, and converts handler callbacks into `S.Diag()` calls. Gated by `EnableFlowNullability` (precomputed from LangOpts and diagnostic state).

**`test/SemaCXX/flow-nullability-*.cpp`** — C++ tests in `test/SemaCXX/`, C tests in `test/Sema/`. This matches ThreadSafety's test layout. Tests are consolidated into a few large files rather than many small ones.

### Dataflow analysis details

Per-edge state tracking: `EdgeStates[{PredBlockID, SuccBlockID}]` stores the narrowing state along each CFG edge. This lets branch-refined narrowing (e.g., true vs false branch of `if (p)`) propagate correctly. Entry state for each block is computed by intersecting edge states from all predecessors — narrowed only if ALL paths agree.

`getTerminalCondition()` extracts the actual sub-expression being tested in each CFG block. The CFG decomposes `&&`/`||` into separate blocks, but the terminator expression is the full `p && q`. This helper recursively follows the RHS of `&&`/`||` to find the leaf that's actually being evaluated in that block.

Three narrowing sets in `NullState`:
- `NarrowedVars` — `DenseSet<const VarDecl*>` for local variables and parameters
- `NarrowedMembers` — `DenseSet<pair<VarDecl*, FieldDecl*>>` for `var->field` member accesses
- `NarrowedThisMembers` — `DenseSet<const FieldDecl*>` for `this->field` member accesses

Transfer functions handle: `DeclStmt` (nonnull init), `BinaryOperator` (assignment invalidation), `UnaryOperator` (`*` deref check, `++`/`--` invalidation), `MemberExpr` (`->` deref check), `ArraySubscriptExpr` (subscript deref check), `CallExpr` (`__builtin_assume` narrowing).

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

- **`nullsafe-clang-dev`** — the full fork with playground, install scripts, CI, WASM build, docs, etc. **All development happens here. Always work on this branch.**
- **`nullsafe-upstream`** — clean branch with only the core compiler changes (68 files), used for the upstream PR to `llvm/llvm-project`. **Never work directly on this branch** — it is rebuilt from `nullsafe-clang-dev` via `sync-upstream.sh`.

Run `./tools/sync-upstream.sh` to rebuild `nullsafe-upstream` from the current state of `nullsafe-clang-dev`. It filters out all fork-only files and creates a single commit on top of `llvm/main`.

**When adding new files:** if the file is part of the compiler feature (belongs in the upstream PR), make sure it's not caught by `EXCLUDE_PATTERNS` in `tools/sync-upstream.sh`. If the file is fork-only (playground, CI, docs, benchmarks, etc.), add a matching pattern to `EXCLUDE_PATTERNS` so it doesn't leak into the upstream branch.

## Conventions

- This is a compiler — correctness matters above all. Every change should have a lit test.
- Use `// expected-warning` and `// expected-error` in lit tests per clang convention.
- Diagnostic messages go in `DiagnosticSemaKinds.td`, referenced via `diag::warn_*` enums.
- When the compiler prints pointer types, `_Nullable` may appear in the printed type (e.g., `'int * _Nullable'`). Account for this in test expected-warning strings.
