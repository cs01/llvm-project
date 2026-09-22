# Nullability safety: review follow-up plan

Living worksheet for the cleanup that came out of the 2026-09-22 design review
of the flow-sensitive nullability analysis. A fresh contributor should be able
to continue from this file alone. Fork-only (not carried to the upstream PR
branch by `tools/sync-upstream.sh`).

## Gates (run for every analysis change)

`tools/nullability-gates.sh <tag>` does all of it and exits nonzero if any
gate fails; `--diff <old> <new>` shows the gained/lost sqlite lines between
two runs. Typical loop:

```bash
tools/nullability-gates.sh --base base   # HEAD, uncommitted changes set aside
tools/nullability-gates.sh new           # working tree
tools/nullability-gates.sh --diff base new
```

`--base` includes untracked files in its stash entry, so new test files
don't leak into the baseline, and restores that entry by commit hash rather
than by stack position. `--skip-sqlite` is the only way to pass without the
sqlite amalgamation.

Don't hand-roll `git stash && ... && git stash pop`: on a clean tree the
stash is a no-op and the pop restores an unrelated older stash. `--base`
stashes only when there is something to stash and pops that entry by name.

1. Build clang (`BUILD_DIR`, default `build-arm` if present, else `build`).
2. Lit: every `clang/test/*/nullability-safety*` and
   `clang/test/SemaCXX/nullability-default*` test passes.
3. sqlite differential (`SQLITE`, default `~/git/sqlite/sqlite3.c`; build it
   with `./configure && make sqlite3.c`): sorted warning lists in
   `-fnullability-default=nonnull` and `=nullable` modes plus the sorted
   evidence remark list. Every gained or lost line is explained in this file
   (commit messages are one line). Counts depend on the sqlite version and
   platform headers, so compare runs on one machine only. Reference counts
   after step 2 (macOS, `build-arm`): nonnull 131, nullable 22849, evidence
   19620. After the rebase (Linux devserver, `build`, node's vendored sqlite
   3.53.1 at `~/git/node/deps/sqlite/sqlite3.c`): nonnull 139, nullable
   22642, evidence 19385.
4. `git clang-format --diff HEAD -- clang` is empty (C++ under `clang/`
   only; the playground's JS and demo files keep their own layout).

The script passes `-fnullability-safety`, so `--base` on a commit from
before step 6 fails; use a checkout of the old script for such baselines.

## Status

| # | Step | State |
|---|------|-------|
| 1 | Report diagnostics and evidence only from converged dataflow states (silent fixpoint, then one reporting pass) | done (fixed contradictory `returns nonnull` + `returns nullable` evidence for one return) |
| 2 | One `classifyStoredValue()` / `storePointer()` for every pointer store (var init, var assign, member assign, aggregate init, ctor-init evidence) | done |
| 3 | Restore `llvm/` and Lex files to upstream; one playground wasm build script shared with CI; allowlist `sync-upstream.sh`; untrack junk | done |
| - | Rebase onto `llvm/main` (2026-09-22); default branch renamed `nullsafe-clang-dev` -> `nullability-safety` | done; gates green on the rebased tree (lit 50/50, sqlite nonnull/nullable/evidence byte-identical to the pre-rebase step 2 lists) |
| - | Gates script exits nonzero on any failure (missing sqlite included, unless `--skip-sqlite`); `--base` replaces the hand-rolled stash recipe | done |
| 4 | Stop tagging types with `_Null_unspecified` unless `-fnullability-default=nullable` (the only mode where the tag changes results); drop the duplicate null-init warning (`warn_null_init_nonnull` vs flow `warn_flow_nullable_assignment`) | done (sqlite below) |
| 6 | Rename to **NullabilitySafety** everywhere (moved before 5 so new files get final names); update the gates script's filename globs; decide explicitly whether old flag spellings stay as aliases | done: hard cut, no aliases (below) |
| 5a | API: options struct, summary oracle split from the handler, drop the unused `SrcExpr` parameter, one Sema opt-in predicate | done (below) |
| F0 | Reduced real-bug regression tests (`SemaCXX/nullability-safety-reduced-real-bugs.cpp`); must keep passing through every F step | done |
| F1 | Smart pointers: in nonnull mode an unchecked smart pointer takes the declared default like a raw pointer; explicit taint for default construction, `= nullptr`, `release()`, `swap()` | done (below) |
| F1b | libstdc++ `shared_ptr`: `s->` / `*s` resolve to the base class `__shared_ptr_access`, whose type is not a smart pointer, so no dereference is checked (both modes, predates F1; libc++ and `unique_ptr` are fine) | todo |
| F4 | Output parameters: a pointer escaping as `&p` to `T **` or binding to `T *&` / `const T *&` loses its nullable facts and guards (reuse `invalidateBoolGuardsFor` / `invalidateMembersFor`); narrowing is kept | done (below) |
| F5 | Lambdas: drop the call-site `IsLambdaCall` nonnull promotion; argument check only for `_Nonnull` or `nonnull(N)` (first parameter is `nonnull(2)`) | done, nonnull mode only (below) |
| 5b | SSAF extractor alongside the remarks; parity check: every remark has a matching summary entry on sqlite; argument evidence assumes callee contracts (below) | todo |
| 5c | SSAF whole-program propagation (below) | todo |
| 5d | SSAF source transformation; then delete the remarks, the `handle*Evidence` callbacks, and the remark-scraping loop | todo |
| F2a | Ternary implications: reverse direction, pointer/comparison/conjunction antecedents, transitive narrowing via worklist | todo |
| 7 | Comment pass: drop history/what-only comments, fix wrong ones, ASCII only | todo |

## False-positive track (F steps)

From a scan of large real-world C++ code under
`-fnullability-default=nonnull`: hundreds of warnings, the smart-pointer
ones none of them a real bug. Governing rule: a missed
bug is acceptable, a spurious warning is not. Design, trade-offs and the
target-behavior lit tests for each step are in the local worksheet
`docs/nullability-safety-fp-classes.md`, which cites internal code and stays
untracked: this branch is pushed to a public fork, so nothing here (plan,
tests, commit messages) may name internal projects, paths or code.

Per F step: copy that step's test from the worksheet into
`clang/test/SemaCXX/`, confirm it is red only on the lines the worksheet
lists, implement, run the gates (sqlite diffs explained here as usual),
confirm `nullability-safety-reduced-real-bugs.cpp` still passes. Order: F0,
F1, F4, F5 (small, independent of SSAF), then 5b-5d, then F2a (largest,
riskiest). Rejected: correlations lost at joins, arithmetic correlations
(suppress per line). Assertion handlers: document `analyzer_noreturn`, no
analysis change.

## F1 results

`isSmartPointerMaybeNull`: flow-nullable or explicitly `_Nullable` warns,
narrowed or `_Nonnull` is trusted, otherwise the mode default (trusted only
under `nonnull`). Used for `->`, `*`, and `V *v = u.get()`. New explicit
taint: default and `nullptr` construction, init or assignment from a
`_Nullable`-returning call, `= nullptr`, `release()`, member and `std::swap`
(facts exchanged). Assigning a local smart pointer now clears its nullable
fact as well as its narrowing, so default-construct-then-assign is silent.
`checker-gaps.cpp` GAP 5 moved to partial (use after `release()` now warns).
Checked against real libstdc++ `<memory>` in nonnull mode: factory results
and default-then-assigned are silent; default, `release`, both swaps,
moved-from and `reset` warn. sqlite: no change (C). Re-scan the internal
codebases to measure the effect; the worksheet predicts most of the 729
smart-pointer sites disappear.

## F4 results

`pointerWritableByCallee` / `escapeToCallee` in `checkCallArguments`:
`&p` to a non-const `T **`, `p` bound to a non-const `T *&` (including
`const T *&`), or `pp` whose `AddrOfTargets` entry is `p` clears `p`'s
nullable fact, its nullable member paths, the guards that name it or are
keyed on it, and `AddrOfTargets[p]`. Narrowing and aliases are kept.

Aliases on purpose: invalidating them was tried and gained one warning in
both modes at sqlite `allocateBtreePage` (`pPrevTrunk = pTrunk`, then
`&pTrunk` escapes, then `if (!pPrevTrunk) ... else pTrunk->aData`). The
stale alias narrowing hid a path the analysis cannot rule out (the error arm
that skips the escape exits via `if (rc) goto`, which it does not model).
Keeping the alias is a possible miss, dropping it is a certain false
positive; `*pp = X` does not invalidate aliases either.

sqlite vs F1: nonnull lost 32 / gained 0 (output-parameter false positives,
mostly `MemPage *` / `PgHdr *` / `Expr *` locals filled through `&p`),
nullable 0 / 0. Evidence lost 67 / gained 64: 64 argument remarks flip from
`nullable` to `nonnull` where the argument was last written by a callee (the
stale `= 0` no longer counts), and 3 `assigned from nullable source` member
remarks disappear because the source is no longer provably nullable. Lit
54/54, including the stale-guard false negative the escape fixes.

## F5 results

The call-site promotion of unannotated lambda parameters to nonnull is now
skipped only under `-fnullability-default=nonnull`, not dropped everywhere as
the worksheet proposed. In nullable mode the lambda body trusts its
parameters (auto-narrowing) while a function body does not, so the call site
is the only check left for a lambda; dropping it there would lose
`work(maybe)` into `n->size` (`nullability-safety-analysis.cpp`,
`lambda_callsite_check`). Under the nonnull default a function's unannotated
parameter is not argument-checked either, so skipping it there is parity.
The worksheet's lambda test was missing the argument warning's note; fixed.
sqlite: no change (no lambdas). Nonnull total now 107 (139 before F4).

`nullsafe-upstream` keeps its name: it is the head of llvm PR #189131, and
GitHub cannot retarget a PR's head branch.

## Step 5a results

- `runNullabilitySafetyAnalysis(AC, Handler, Options, Summaries)`:
  `NullabilitySafetyOptions` holds `DefaultNullability` and
  `LibcNullableReturns`; `NullabilitySafetySummaries` (nullable pointer) holds
  the one cross-function query, `isKnownAllReturnsNonnull`, so the handler is
  output only. Sema implements it as `AllReturnsNonnullSummaries` over the
  TU-local set; 5c's SSAF results are meant to be a second implementation.
- `Sema::diagnoseNullableToNonnullConversion` is back to the upstream
  signature: the `SrcExpr` argument the fork added was never read, and its
  four call sites now match upstream.
- `handleNullableReturn` lost its `ExprType` / `ReturnType` parameters, which
  the reporter never used.
- `Sema::isNullabilitySafetyOptedIn(const Decl *)` is the one opt-in
  predicate, used by `getAnalyzableDecl` and by the legacy
  `warn_nullability_lost` suppression; `functionHasNullabilityAnnotations`
  and `declHasNullabilityAnnotations` became file-static in `Sema.cpp`.

sqlite vs the libc-flag rename commit: 0 lost / 0 gained in all three modes;
lit 51/51.

## Argument evidence assumes callee contracts (not a bug)

```c
void take(int *_Nonnull p);
void f(int *_Nullable q) { take(q); }
// warning: passing nullable pointer to nonnull parameter 'p'
// remark: parameter 'p' of 'take' ... called with nonnull argument
```

Looks contradictory, is deliberate (`24aadeefa793`, locked by `two_params`
in `SemaCXX/nullability-safety-evidence-unspecified.cpp`): evidence is
judged after the call's nonnull-parameter narrowing, i.e. assuming the
contract held. The violation is reported once, as the warning; judging the
argument before narrowing would also give the call's other parameters
nullable evidence (`take_two(q, q)` with only the first `_Nonnull`), which 5c
would propagate into `_Nullable` and new warnings inside the callee, a
cascade from one already-reported bug. The SSAF extractor keeps these
semantics.

## Step 6 results

Decision: hard cut. `-fflow-sensitive-nullability`, `-Wflow-nullability` and
`-Wflow-nullable-*` are gone, not aliased; in-repo consumers (release and CI
workflows, install script, playground, benchmarks, docs) were updated in the
same commit. Anyone still passing the old flag gets `unknown argument`.

Renamed per the Naming table, plus: `FlowNullabilityReporter` /
`FlowNullabilityTUAnalysis` in `AnalysisBasedWarnings.cpp`, `DEBUG_TYPE`
`nullability-safety`, every `flow-nullability-*` test and doc file, and
`Driver/nullsafe-flags*.c` -> `Driver/nullability-safety-flags*.c`. The
`nullsafe` product names (playground, headers, `-Rnullsafe-evidence`,
`remark_nullsafe_*`) are unchanged; the remarks go away in 5d.

sqlite (group names in the baseline normalized to the new spelling): nonnull
0 lost / 0 gained, nullable 0 / 0, evidence 0 / 0; counts unchanged (139,
22642, 19385); lit 51/51.

## Step 4 results

sqlite vs the post-rebase baseline: nullable 0 lost / 0 gained, evidence
0 / 0, nonnull 129 / 129 where every pair is the same warning with the type
printed without `_Null_unspecified` (`'Db * _Null_unspecified'` ->
`'Db *'`, `'char * *'` -> `'char **'`). No warning appeared or disappeared.

Not in the original design: under the nonnull default an untagged
declaration falls through to `checkNullabilityConsistency`, which would
fire `-Wnullability-completeness` on every bare pointer in a partially
annotated header. The tag had been suppressing that, so the nonnull branch
now sets `CAMN_No` instead. `Sema/flow-nullability-default-tagging.c` covers
it (verified to fail without the guard). The sqlite gate cannot see this
because it runs with `-Wno-everything`.

## Step 4 design

- Type tagging: `SemaType.cpp` injects `_Null_unspecified` in three places
  (single-level pointers, multi-level pointers, and the local/cast/template
  argument contexts under `FlowSensitiveNullability`). The tag only changes
  analysis results under `-fnullability-default=nullable` (with `unspecified`
  or `nonnull` an untagged pointer reads the same), so gate all three on
  `getNullabilityDefault() == NullabilityKind::Nullable`. Then the flag alone
  no longer rewrites types in unrelated diagnostics (repro: `char c = q;` with
  `int *q` prints `'int * _Null_unspecified'` under just
  `-fflow-sensitive-nullability`). Expect test expectations that spell
  `_Null_unspecified` under nonnull mode to change.
- Duplicate warning: `int *_Nonnull p = 0;` warns twice (`SemaDecl.cpp`
  `warn_null_init_nonnull` and the flow `warn_flow_nullable_assignment`). Keep
  the Sema one (type-based, also covers globals the flow never sees); in
  `storePointer`, skip the report when a variable's initializer is a null
  pointer constant, still marking the variable nullable. Assignments stay
  flow-reported.

## Resuming on another machine

The loop was: do the next step in the status table, run the gates before and
after, explain every sqlite diff line in this file, commit (one-line
message), update this table. Tell the agent: "continue docs/nullability-safety-plan.md". The
branch `nullsafe-clang-dev` (pre-rebase history) and
`backup/pre-rebase-2026-09-22` (local only on the original machine) are the
fallbacks.

## Naming

**NullabilitySafety**, mirroring upstream `LifetimeSafety` / `ThreadSafety`
(named for the property checked, not the technique). Neither neighbor is sound
either; both document their limitations, and so must we.

| Thing | Before | Target |
|---|---|---|
| Files | `FlowNullability.{h,cpp}` | `NullabilitySafety.{h,cpp}` |
| API | `runFlowNullabilityAnalysis`, `FlowNullabilityHandler` | `runNullabilitySafetyAnalysis`, `NullabilitySafetyHandler` |
| Flag / langopt | `-fflow-sensitive-nullability` / `FlowSensitiveNullability` | `-fnullability-safety` / `NullabilitySafety` |
| Warnings | `-Wflow-nullability`, `-Wflow-nullable-*` | `-Wnullability-safety`, `-Wnullability-safety-*` |
| Diag IDs | `warn_flow_nullable_*`, `warn_null_init_nonnull` | `warn_nullability_safety_*` |
| Evidence | `-Rnullsafe-evidence` remarks | SSAF summaries (below) |
| C library list | `-fnullability-stdlib-annotations` | `-fnullability-libc-nullable-returns` (it never covered the C++ STL list, which has no flag) |
| Unchanged | `-fnullability-default=` | |

Rejected: NullSafety (overpromises a sound Kotlin-style guarantee, leaves
clang's "nullability" vocabulary), NullChecker ("checker" means a Static
Analyzer plugin), NullAnalysis / CFGNullChecker (generic, names the technique).

## Evidence via SSAF

Replace the `-Rnullsafe-evidence` remarks, the four `handle*Evidence` handler
callbacks, and the remark-scraping annotation loop with the upstream Scalable
Static Analysis Framework (`clang/lib/ScalableStaticAnalysis`, docs in
`clang/docs/ScalableStaticAnalysis`; LLVM Dev Meeting 2026 talk "Building
interprocedural static analyses in Clang with SSAF", Jan Korous and Aviral
Goel). The in-tree UnsafeBufferUsage analysis is the template for every layer.

| Layer | SSAF piece | Ours |
|---|---|---|
| Per TU | `TUSummaryExtractor` (template: `UnsafeBufferUsageExtractor.cpp`, which reuses the `lib/Analysis` code the Sema warning uses) | Extractor runs the analysis with an evidence-collecting handler; records nonnull / nullable evidence per `EntityPointerLevel` (from `DeclPointerLevel{Decl, level, IsReturn}`: parameter, field, return) |
| Whole program | `DerivedAnalysis` over `PointerFlowAnalysisResult` + our summary result (template: `UnsafeBufferReachableAnalysis`) | Seed with nullable evidence, propagate along pointer-flow edges **in reverse**; nonnull = nonnull evidence with no nullable reaching it |
| Output | `SourceTransformation` -> `clang-apply-replacements` YAML + SARIF (template: `CppBoundedBuffers.cpp`) | Insert `_Nullable` / `_Nonnull` at each entity's declaration |

Edge direction (verified): `PointerFlow.h` maps each LHS (assignee) to the
RHS values assigned to it, while nullability flows from value to assignee,
so propagation walks edges RHS -> LHS. `TypeConstrainedPointers` lists
pointers that must keep their pointer type (`main` parameters, `operator
new`/`delete`); it guards type-replacing rewrites and is not needed to add
qualifiers, though it may serve as a do-not-annotate list.

End-to-end lit pattern: `clang/test/Analysis/Scalable/source-edit-generation/cpp-bounded-buffers-replacements.cpp`
(extract -> `clang-ssaf-linker` -> `clang-ssaf-analyzer` -> transform).
