# Upstream PR Review — Flow-Sensitive Nullability Analysis

Tracking fixes needed before submitting to LLVM upstream.
Branch: `nullsafe-upstream` (PR target: `llvm/main`)

## High Priority

- [x] **H1: Address-taken variable invalidation** — Documented as intentional design choice (matching ThreadSafety). Added doc comment to `handleCallExpr` explaining the trade-off.

- [x] **H2: SemaType.cpp nullability inference without flow flag** — **BUG FIXED.** The `DeclaratorContext::TypeName`/`FunctionalCast` block was unconditionally inferring `Unspecified` on all local pointer types, even without `-fflow-sensitive-nullability`. Added `FlowSensitiveNullability` guard.

- [x] **H3: SemaCast.cpp setType() safety** — Verified safe: `setType()` runs immediately after `CXX*CastExpr::Create`, before the node is inserted into any parent or cache. Improved comment to clarify.

- [x] **H4: isExprProvablyNonnull stack depth** — Verified safe: depth-16 limit, each frame peels one AST node, negligible stack usage. Added comment explaining the depth limit rationale.

- [x] **H5: DenseSet fixpoint comparison cost** — Documented as future optimization (BitVector). Current perf is fine per stress test. Added doc comment to `NullState`.

## Medium Priority

- [x] **M1: Document missing `||` decomposition** — Added doc comment to `analyzeCondition` explaining why `||` is not decomposed (CFG handles it naturally).

- [x] **M2: System header macro leakage** — Non-issue. `isInSystemHeader(D.getBeginLoc())` checks the expansion location, so system macros expanded in user code correctly get user settings.

- [x] **M3: Conversion function exclusion test** — Existing test `flow-nullability-conversion-op.cpp` already covers this. Added explanatory comment to the test.

- [x] **M4: warn_null_init_nonnull in dead code** — Confirmed: fires in `constexpr if` discarded branches. Suppression requires tracking discarded-statement context at declaration processing time, which Clang doesn't expose in `AddInitializerToDecl`. Documented as known limitation (rare scenario: explicit `_Nonnull p = nullptr` in discarded branch).

## Low Priority / Nits

- [x] **L1: Extra blank line** — Fixed in FlowNullability.cpp.

- [x] **L2: Comment style** — Was already using `///`. Non-issue.

- [ ] **L3: Test directory structure** — Move 44 `flow-nullability-*` test files into `clang/test/Sema/FlowNullability/` subdirectory. Upstream reviewers will likely request this.

- [ ] **L4: Verify -std=c++17 on all C++17 tests** — Structured bindings, if constexpr tests need the flag.

## Architecture Questions (for RFC / commit message)

- [ ] **A1: Why not clang::dataflow framework?** — Prepare justification for bespoke worklist+lattice vs `DataflowAnalysis.h`.
- [ ] **A2: Interaction with -Wnullable-to-nonnull-conversion** — Document migration story for existing users.
- [ ] **A3: _Null_unspecified semantics** — Document the "use the default" design choice in Clang docs or commit message.

## Join optimization (future)

- NullState::join allocates fresh DenseSets every call. In-place intersection would reduce allocation churn for hot loops. Not blocking.
