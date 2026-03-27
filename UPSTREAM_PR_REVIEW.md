# Upstream PR Review — Flow-Sensitive Nullability Analysis

Tracking fixes needed before submitting to LLVM upstream.
Branch: `nullsafe-upstream` (PR target: `llvm/main`)

## Critical — AST Mutation Issues

These three changes modify AST types to encode analysis results. LLVM reviewers
will likely reject them because the AST is the shared representation and should
not be mutated by an optional analysis. All three are redundant with the flow
analysis in FlowNullability.cpp which already handles these cases.

- [x] **C1: Address-of bakes `_Nonnull` into AST type** — `SemaExpr.cpp:15109-15112` wraps the result of `&x` in `attr::TypeNonNull`. This changes `decltype(&x)`, affects template argument deduction, AST dumps, and modules serialization. The flow analysis already handles address-of via `isNonnullInit` (UO_AddrOf check) and `handleDeclStmt` (line 592-594). **Fix:** Remove the `getAttributedType` call in `CheckAddressOfOperand`; rely on flow analysis.

- [x] **C2: `operator new` bakes `_Nonnull` into `CXXNewExpr` type** — `SemaExprCXX.cpp:2666-2679` wraps `ResultType` in `attr::TypeNonNull` before `CXXNewExpr::Create`. Same observability problems as C1. The flow analysis already handles this via `isNonnullInit` which checks `CXXNewExpr::shouldNullCheckAllocation()`. **Fix:** Remove the `ResultType` modification in `BuildCXXNew`; rely on flow analysis.

- [x] **C3: Cast propagation mutates AST via `setType()`** — `SemaCast.cpp:163-178` calls `castExpr->setType()` to propagate nullability through casts. `setType()` on AST nodes is unsafe in general (maintenance hazard if caching/parent-linking is added between creation and this point). The flow analysis already handles casts via `unwrapCastsAndArithmetic` (line 466-486). **Fix:** Remove the `setType` block in SemaCast; rely on flow analysis.

- [ ] **C4: SemaType.cpp changes type identity for local pointers** — With `-fflow-sensitive-nullability`, all local `int*` gains `_Null_unspecified`. This could affect C++ name mangling (Apple platforms), `std::is_same`, and modules compatibility. The flag guard mitigates risk but **needs tests**: add tests for `decltype`, `std::is_same`, and template argument deduction to verify no breakage.

## High Priority

- [x] **H1: Address-taken variable invalidation** — Documented as intentional design choice (matching ThreadSafety). Added doc comment to `handleCallExpr` explaining the trade-off.

- [x] **H2: SemaType.cpp nullability inference without flow flag** — **BUG FIXED.** The `DeclaratorContext::TypeName`/`FunctionalCast` block was unconditionally inferring `Unspecified` on all local pointer types, even without `-fflow-sensitive-nullability`. Added `FlowSensitiveNullability` guard.

- [x] **H3: SemaCast.cpp setType() safety** — Superseded by C3 above. Should be removed entirely rather than verified safe.

- [x] **H4: isExprProvablyNonnull stack depth** — Verified safe: depth-16 limit, each frame peels one AST node, negligible stack usage. Added comment explaining the depth limit rationale.

- [x] **H5: DenseSet fixpoint comparison cost** — Documented as future optimization (BitVector). Current perf is fine per stress test. Added doc comment to `NullState`.

- [ ] **H6: Duplicate `isExprProvablyNonnull` vs `isNonnullInit`** — `Sema.cpp:681-720` and `FlowNullability.cpp` have overlapping logic for determining non-null provenance (address-of, CXXNewExpr, this, pointer arithmetic, casts). A bug fix in one won't be applied to the other. **Fix:** Factor into a shared utility in `clang/lib/Analysis/`, or at minimum add cross-reference comments. If C1-C3 are removed, `isExprProvablyNonnull` may become unnecessary.

- [ ] **H7: `sp = someFunction()` always narrows (false negative)** — `FlowNullability.cpp:950-955` assumes all functions returning `unique_ptr` by value return non-null. But `return {}` or `return nullptr` produces an empty unique_ptr. **Fix:** Remove lines 950-955 (the `else` block that unconditionally narrows). Only narrow on `make_unique`/`make_shared` (already handled at line 937-939).

- [ ] **H8: Address-taken pointer false negative needs test** — `void nullify(int** out)` can set `*out = nullptr`, invalidating narrowing on the pointed-to variable. The analysis intentionally doesn't invalidate (matching ThreadSafety), but there's no test documenting this known false negative. **Fix:** Add an explicit test case with a `// known false negative` comment.

- [ ] **H9: `constexpr if` discarded branch false positive needs test** — `warn_null_init_nonnull` fires in discarded branches. Add a test case with `// FIXME` comment and document in the commit message as a known limitation.

## Medium Priority

- [x] **M1: Document missing `||` decomposition** — Added doc comment to `analyzeCondition` explaining why `||` is not decomposed (CFG handles it naturally).

- [x] **M2: System header macro leakage** — Non-issue. `isInSystemHeader(D.getBeginLoc())` checks the expansion location, so system macros expanded in user code correctly get user settings.

- [x] **M3: Conversion function exclusion test** — Existing test `flow-nullability-conversion-op.cpp` already covers this. Added explanatory comment to the test.

- [x] **M4: warn_null_init_nonnull in dead code** — Confirmed: fires in `constexpr if` discarded branches. Suppression requires tracking discarded-statement context at declaration processing time, which Clang doesn't expose in `AddInitializerToDecl`. Documented as known limitation (rare scenario: explicit `_Nonnull p = nullptr` in discarded branch).

## Low Priority / Nits

- [x] **L1: Extra blank line** — Fixed in FlowNullability.cpp.

- [x] **L2: Comment style** — Was already using `///`. Non-issue.

- [ ] **L3: Test directory structure** — Move 44 `flow-nullability-*` test files into `clang/test/Sema/FlowNullability/` subdirectory. Upstream reviewers will likely request this.

- [ ] **L4: Verify -std=c++17 on all C++17 tests** — Structured bindings, if constexpr tests need the flag. Also `flow-nullability-warning-groups.cpp` doesn't specify a standard.

- [ ] **L5: Diagnostic wording** — `warn_null_init_nonnull` says "null assigned to a variable" but it's an initialization, not assignment. Change to "null used to initialize a variable of nonnull type %0" or similar.

- [ ] **L6: Remove `UPSTREAM_PR_REVIEW.md` from upstream branch** — This tracking doc should not be in the PR. Keep on `nullsafe-clang-dev` only.

## Architecture Questions (for RFC / commit message)

- [ ] **A1: Why not clang::dataflow framework?** — Prepare justification:
  1. Edge-state model: analysis uses per-edge states, not per-block join (framework doesn't support edge-splitting natively)
  2. Simplicity: ~400 lines of dataflow logic vs framework's heavyweight Environment/StorageLocation/Value hierarchy
  3. Precedent: ThreadSafety, Consumed, UninitializedValues all use bespoke analyses
  4. Performance: DenseSet approach is leaner than per-block environments

- [ ] **A2: Interaction with -Wnullable-to-nonnull-conversion** — Document migration story for existing users.

- [ ] **A3: _Null_unspecified semantics** — Document the "use the default" design choice in Clang docs or commit message.

- [ ] **A4: Consider splitting into two patches** — Patch 1: flags + FlowNullability.cpp + AnalysisBasedWarnings integration (clean, self-contained). Patch 2: SemaType.cpp nullability inference changes (more invasive, can be reviewed separately). This reduces review burden and risk. (Only relevant if C1-C3 are removed; otherwise the Sema changes are more entangled.)

## Missing Test Coverage

- [ ] `decltype(&x)` and `decltype(new T)` — verify they don't gain `_Nonnull` (critical if C1/C2 not fixed)
- [ ] Template argument deduction through `&x` and `new T` — verify no SFINAE/overload breakage
- [ ] `-fnullability-default=nullable` with STL headers (`<string>`, `<vector>`) — verify no warnings from system headers
- [ ] PCH/modules round-trip — verify types serialize correctly with inferred nullability
- [ ] Both `-Wflow-nullable-dereference` and `-Wnullable-to-nonnull-conversion` active simultaneously
- [ ] Address-taken pointer false negative (see H8)
- [ ] `constexpr if` discarded branch false positive (see H9)

## Join optimization (future)

- NullState::join allocates fresh DenseSets every call. In-place intersection would reduce allocation churn for hot loops. Not blocking.
