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
- `test/Sema/flow-nullability-arrow-deref.cpp` - arrow operator nullability checks
- `test/Sema/flow-nullability-gradual-adoption.cpp` - per-function gradual adoption gating
- `test/Sema/flow-nullability-ternary.cpp` - ternary operator narrowing (`p ? *p : 0`)
- `test/Sema/flow-nullability-noreturn.cpp` - noreturn functions, if-else termination, do-while assertions
- `test/Sema/flow-nullability-builtin-expect.cpp` - `__builtin_expect`/LIKELY/UNLIKELY/CHECK macros
- `test/Sema/flow-nullability-else-branch.cpp` - else-branch narrowing including OR conditions
- `test/Sema/flow-nullability-for-loop.cpp` - for-loop increment narrowing
- `test/Sema/flow-nullability-brace-assert.cpp` - bare-brace assertion macros (`{ if (!(p)) abort(); }`)
- `test/Sema/flow-nullability-smart-ptr.cpp` - smart pointer `operator->` false positive suppression
- `test/Sema/flow-nullability-call-invalidation.c` - function calls do NOT invalidate narrowing
- `test/Sema/flow-nullability-while-loop.cpp` - while-loop condition narrowing, linked-list traversal
- `test/Sema/flow-nullability-reassignment.cpp` - reassignment invalidates narrowing
- `test/Sema/flow-nullability-nonnull-param.cpp` - `_Nonnull` parameters never warn
- `test/Sema/flow-nullability-default-nonnull.cpp` - `-fnullability-default=nonnull` mode
- `test/Sema/flow-nullability-switch.cpp` - narrowing visibility inside switch cases
- `test/Sema/flow-nullability-terminators.cpp` - throw/goto/break/continue post-dominator narrowing
- `test/Sema/flow-nullability-and-shortcircuit.cpp` - `&&` short-circuit narrowing
- `test/Sema/flow-nullability-array-subscript.cpp` - array subscript checked as dereference
- `test/Sema/flow-nullability-c-basic.c` - basic narrowing in C mode
- `test/Sema/flow-nullability-address-of.cpp` - address-of operator narrowing
- `test/Sema/flow-nullability-cast-propagation.cpp` - nullability propagation through casts
- `test/Sema/flow-nullability-conversion-op.cpp` - conversion operator handling
- `test/Sema/flow-nullability-new-expr.cpp` - `new` expression produces nonnull
- `test/Sema/flow-nullability-nullable-default-template.cpp` - nullable-default with template return types
- `test/Sema/flow-nullability-range-for.cpp` - range-for loop narrowing
- `test/Driver/nullsafe-flags.c` - driver flag forwarding

Run a specific lit test:
```bash
build/bin/llvm-lit test/Sema/flow-nullability-arrow-deref.cpp -v
```

## Key Custom Flags

- `-fflow-sensitive-nullability` - enables flow-sensitive nullability analysis
- `-fnullability-default=nullable|nonnull|unspecified` - sets default nullability for unannotated pointers
- `-fstrict-nullability-inference` - treats inferred nullability as explicit

## Key Files (Nullsafe Changes)

- `lib/Analysis/FlowNullability.cpp` - CFG-based forward dataflow analysis: nullability narrowing, dereference checking, condition analysis, per-edge state tracking
- `include/clang/Analysis/Analyses/FlowNullability.h` - analysis interface: `FlowNullabilityHandler` callback, `runFlowNullabilityAnalysis` entry point
- `lib/Sema/AnalysisBasedWarnings.cpp` - wires the analysis into Clang's warning pipeline: `FlowNullabilityReporter`, `shouldEnableFlowNullability`, CFG build options
- `lib/Sema/SemaDecl.cpp` - gradual adoption gating: sets `FlowSensitiveNullabilityEnabled` per-function in `ActOnStartOfFunctionDef`
- `include/clang/Sema/Sema.h` - `FlowSensitiveNullabilityEnabled` flag, `FunctionHasNullabilityAnnotations`
- `lib/Sema/Sema.cpp` - `FunctionHasNullabilityAnnotations`, `diagnoseNullableToNonnullConversion`
- `lib/Driver/ToolChains/Clang.cpp` - driver-to-cc1 flag forwarding
- `include/clang/Driver/Options.td` - flag definitions
- `include/clang/Basic/DiagnosticSemaKinds.td` - `warn_strict_nullability_dereference` diagnostic
- `include/clang/Basic/DiagnosticGroups.td` - `FlowNullableDereference` / `FlowNullability` diagnostic groups

## Architecture

The analysis follows the same pattern as Clang's ThreadSafety and UninitializedValues analyses: a standalone analysis in `lib/Analysis/` invoked from `AnalysisBasedWarnings.cpp`, reporting results via a handler interface.

### Three-layer design

**`lib/Analysis/FlowNullability.cpp`** — the analysis algorithm. Operates on the CFG (control flow graph), which Clang builds automatically from the AST. Uses `ForwardDataflowWorklist` for fixpoint iteration over CFG blocks in reverse-post-order. Tracks `NullState` (sets of narrowed variables and members) per edge, intersecting at merge points. Reports dereferences of nullable pointers via `FlowNullabilityHandler` callbacks.

**`lib/Sema/AnalysisBasedWarnings.cpp`** — the glue layer. Builds the CFG, instantiates the analysis, and converts handler callbacks into `S.Diag()` calls. Also decides whether to run the analysis per-function via `shouldEnableFlowNullability`.

**`test/Sema/flow-nullability-*.cpp`** — tests live in `test/Sema/` because the diagnostics are Sema diagnostics, even though the analysis itself is CFG-based. This matches how ThreadSafety tests live in `test/SemaCXX/`.

### Dataflow analysis details

Per-edge state tracking: `EdgeStates[{PredBlockID, SuccBlockID}]` stores the narrowing state along each CFG edge. This lets branch-refined narrowing (e.g., true vs false branch of `if (p)`) propagate correctly. Entry state for each block is computed by intersecting edge states from all predecessors — narrowed only if ALL paths agree.

`getTerminalCondition()` extracts the actual sub-expression being tested in each CFG block. The CFG decomposes `&&`/`||` into separate blocks, but the terminator expression is the full `p && q`. This helper recursively follows the RHS of `&&`/`||` to find the leaf that's actually being evaluated in that block.

Three narrowing sets in `NullState`:
- `NarrowedVars` — `DenseSet<const VarDecl*>` for local variables and parameters
- `NarrowedMembers` — `DenseSet<pair<VarDecl*, FieldDecl*>>` for `var->field` member accesses
- `NarrowedThisMembers` — `DenseSet<const FieldDecl*>` for `this->field` member accesses

Transfer functions handle: `DeclStmt` (nonnull init), `BinaryOperator` (assignment invalidation), `UnaryOperator` (`*` deref check, `++`/`--` invalidation), `MemberExpr` (`->` deref check), `ArraySubscriptExpr` (subscript deref check), `CallExpr` (`__builtin_assume` narrowing).

### Gradual adoption

Flow-sensitive checking only activates per-function when inside a `#pragma clang assume_nonnull` region OR when `-fnullability-default` is set to something other than `unspecified`. This is determined in `SemaDecl.cpp:ActOnStartOfFunctionDef` and stored as `FlowSensitiveNullabilityEnabled` on the `Sema` instance.

### Suppressions

- `this->x` arrow dereferences are always suppressed (`this` is never null in C++)
- `*this` dereferences are suppressed for the same reason
- `CXXOperatorCallExpr(OO_Arrow)` (smart pointer `operator->`) is suppressed at the call site
- Function calls do NOT invalidate narrowing (pointers are passed by value)

Analysis is intraprocedural — it does not look inside called functions to determine nullability.

## Conventions

- This is a compiler — correctness matters above all. Every change should have a lit test.
- Use `// expected-warning` and `// expected-error` in lit tests per clang convention.
- Diagnostic messages go in `DiagnosticSemaKinds.td`, referenced via `diag::warn_*` enums.
- When the compiler prints pointer types, `_Nullable` may appear in the printed type (e.g., `'int * _Nullable'`). Account for this in test expected-warning strings.
