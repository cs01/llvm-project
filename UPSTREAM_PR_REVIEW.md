# Upstream PR Review — Flow-Sensitive Nullability Analysis

Open items before submitting to LLVM upstream.
Branch: `nullsafe-upstream` (PR target: `llvm/main`)

**Remove this file from the upstream branch before submitting.**

## Architecture — Needs Documentation

- [ ] **A1: Why not `clang::dataflow` framework?** — Prepare justification for commit message / RFC:
  1. Per-edge state model (framework is per-block)
  2. ~400 lines of dataflow vs framework's Environment/StorageLocation/Value
  3. Precedent: ThreadSafety, Consumed, UninitializedValues all use bespoke analyses
  4. DenseSet approach is leaner than per-block environments

- [ ] **A2: `_Null_unspecified` overloaded as "default-inferred"** — `SemaType.cpp` assigns `NullabilityKind::Unspecified` to unannotated pointers under `-fflow-sensitive-nullability` so the flow checker can distinguish explicit `_Nullable` from default-inferred. This changes the AST representation of types under the flag. Document trade-off in commit message.

- [ ] **A3: Interaction with `-Wnullable-to-nonnull-conversion`** — Document migration story for users already using `-Wnullable-to-nonnull-conversion`.

- [ ] **A4: Consider splitting into two patches** — Patch 1: flags + `FlowNullability.cpp` + `AnalysisBasedWarnings` integration (clean, self-contained). Patch 2: `SemaType.cpp` nullability inference changes (more invasive, affects type identity).

## Medium Priority — Upstream Risks

- [ ] **M1: Local variable nullability inference affects type identity** — With `-fflow-sensitive-nullability`, local `int*` gains `_Null_unspecified` in the AST. Could affect type printing, `std::is_same`, template argument deduction, name mangling on Apple platforms. Gated by the flag. **Needs tests (T2).**

- [ ] **M2: `FlowNullabilityEnabled` as a `Sema` member** — Per-function state idiomatically belongs in `FunctionScopeInfo`. Currently safe (sequential processing), but fragile if nested function processing is introduced. TODO comment added.

- [ ] **M3: `isExprProvablyNonnull` vs `isNonnullInit` duplication** — `Sema.cpp` and `FlowNullability.cpp` have overlapping non-null provenance logic. Cross-reference comments added, but factoring into a shared utility would be cleaner. Low risk since they serve different purposes (type-level suppression vs flow-level narrowing).

## Missing Test Coverage

- [ ] **T1: No `if constexpr` test** — C++17 discarded branches may produce false positives on `_Nonnull p = nullptr` in dead code.

- [ ] **T2: No type identity tests** — `decltype`, `std::is_same<int*, int* _Null_unspecified>`, template argument deduction with inferred nullability. Critical for M1.

- [ ] **T3: No Objective-C test** — Apple SDKs heavily use nullability. A minimal `.m` file would strengthen reviewer confidence.

- [ ] **T4: No duplicate diagnostic test** — When both `-Wnullable-to-nonnull-conversion` and `-Wflow-nullable-dereference` fire on the same code, verify no confusing double warnings.

- [ ] **T5: No cc1 error test for `-fnullability-default=invalid`** — Driver passes invalid values through; no test verifies cc1 rejects them.

- [ ] **T6: No PCH/modules round-trip test** — Verify types serialize correctly with inferred nullability.

- [ ] **T7: No STL header test** — Verify no spurious warnings from `<string>`, `<vector>` under `-fnullability-default=nullable`.

- [ ] **T8: Test directory structure** — 45+ `flow-nullability-*` files in `clang/test/Sema/`. LLVM reviewers may request a `FlowNullability/` subdirectory.

## Performance Notes

Benchmarks show negligible overhead. Include results in PR description.

- `NullState::join` allocates fresh `DenseSet`s per call. In-place intersection would reduce churn, but not a measured bottleneck.
- Per-edge state is O(E) `NullState` objects. Switch stress test (50 cases) added to cover this.
- `invalidateBoolGuardsFor` / `invalidateMembersFor` do linear scans. O(1) with reverse index, but n is small.
