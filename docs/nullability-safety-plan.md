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
2. Lit: every `clang/test/*/flow-nullability*`, `clang/test/Driver/nullsafe*`,
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
4. `git clang-format --diff` is empty.

## Status

| # | Step | State |
|---|------|-------|
| 1 | Report diagnostics and evidence only from converged dataflow states (silent fixpoint, then one reporting pass) | done (fixed contradictory `returns nonnull` + `returns nullable` evidence for one return) |
| 2 | One `classifyStoredValue()` / `storePointer()` for every pointer store (var init, var assign, member assign, aggregate init, ctor-init evidence) | done |
| 3 | Restore `llvm/` and Lex files to upstream; one playground wasm build script shared with CI; allowlist `sync-upstream.sh`; untrack junk | done |
| - | Rebase onto `llvm/main` (2026-09-22); default branch renamed `nullsafe-clang-dev` -> `nullability-safety` | done; gates green on the rebased tree (lit 50/50, sqlite nonnull/nullable/evidence byte-identical to the pre-rebase step 2 lists) |
| - | Gates script exits nonzero on any failure (missing sqlite included, unless `--skip-sqlite`); `--base` replaces the hand-rolled stash recipe | done |
| 4 | Stop tagging types with `_Null_unspecified` unless `-fnullability-default=nullable` (the only mode where the tag changes results); drop the duplicate null-init warning (`warn_null_init_nonnull` vs flow `warn_flow_nullable_assignment`) | done (sqlite below) |
| 6 | Rename to **NullabilitySafety** everywhere (moved before 5 so new files get final names); update the gates script's filename globs; decide explicitly whether old flag spellings stay as aliases | todo |
| 5a | API: options struct, summary oracle split from the handler, drop the unused `SrcExpr` parameter, one Sema opt-in predicate | todo |
| 5b | SSAF extractor alongside the remarks; parity check: every remark has a matching summary entry on sqlite | todo |
| 5c | SSAF whole-program propagation (below) | todo |
| 5d | SSAF source transformation; then delete the remarks, the `handle*Evidence` callbacks, and the remark-scraping loop | todo |
| 7 | Comment pass: drop history/what-only comments, fix wrong ones, ASCII only | todo |

`nullsafe-upstream` keeps its name: it is the head of llvm PR #189131, and
GitHub cannot retarget a PR's head branch.

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

| Thing | Now | Target |
|---|---|---|
| Files | `FlowNullability.{h,cpp}` | `NullabilitySafety.{h,cpp}` |
| API | `runFlowNullabilityAnalysis`, `FlowNullabilityHandler` | `runNullabilitySafetyAnalysis`, `NullabilitySafetyHandler` |
| Flag / langopt | `-fflow-sensitive-nullability` / `FlowSensitiveNullability` | `-fnullability-safety` / `NullabilitySafety` |
| Warnings | `-Wflow-nullability`, `-Wflow-nullable-*` | `-Wnullability-safety`, `-Wnullability-safety-*` |
| Diag IDs | `warn_flow_nullable_*`, `warn_null_init_nonnull` | `warn_nullability_safety_*` |
| Evidence | `-Rnullsafe-evidence` remarks | SSAF summaries (below) |
| Unchanged | `-fnullability-default=`, `-fnullability-stdlib-annotations` | |

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
