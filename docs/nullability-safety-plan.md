# Nullability safety: review follow-up plan

Living worksheet for the cleanup that came out of the 2026-09-22 design review
of the flow-sensitive nullability analysis. A fresh contributor should be able
to continue from this file alone. Fork-only (not carried to the upstream PR
branch by `tools/sync-upstream.sh`).

## Gates (run for every analysis change)

`tools/nullability-gates.sh <tag>` does all of it; `--diff <old> <new>` shows
the gained/lost sqlite lines between two runs. Typical loop:

```bash
git stash && tools/nullability-gates.sh base && git stash pop
tools/nullability-gates.sh new
tools/nullability-gates.sh --diff base new
```

1. Build clang (`BUILD_DIR`, default `build-arm`).
2. Lit: every `clang/test/*/flow-nullability*`, `clang/test/Driver/nullsafe*`,
   `clang/test/SemaCXX/nullability-default*` test passes.
3. sqlite differential (`SQLITE`, default `~/git/sqlite/sqlite3.c`; build it
   with `./configure && make sqlite3.c`): sorted warning lists in
   `-fnullability-default=nonnull` and `=nullable` modes plus the sorted
   evidence remark list. Every gained or lost line is explained in the commit.
   Reference counts after step 2: nonnull 131, nullable 22849, evidence 19620.
4. `git clang-format --diff` is empty.

## Status

| # | Step | State |
|---|------|-------|
| 1 | Report diagnostics and evidence only from converged dataflow states (silent fixpoint, then one reporting pass) | done (fixed contradictory `returns nonnull` + `returns nullable` evidence for one return) |
| 2 | One `classifyStoredValue()` / `storePointer()` for every pointer store (var init, var assign, member assign, aggregate init, ctor-init evidence) | done |
| 3 | Restore `llvm/` and Lex files to upstream; one playground wasm build script shared with CI; allowlist `sync-upstream.sh`; untrack junk | done |
| - | Rebase onto `llvm/main` (2026-09-22); default branch renamed `nullsafe-clang-dev` -> `nullability-safety` | done; gates green on the rebased tree (lit 50/50, sqlite nonnull/nullable/evidence byte-identical to the pre-rebase step 2 lists) |
| 4 | Stop tagging types with `_Null_unspecified` unless a function opted in; drop the duplicate null-init warning (`warn_null_init_nonnull` vs flow `warn_flow_nullable_assignment`) | next |
| 6 | Rename to **NullabilitySafety** everywhere (moved before 5 so new files get final names) | todo |
| 5 | API: options struct, summary oracle split from the handler, drop the unused `SrcExpr` parameter, one Sema opt-in predicate; replace evidence remarks with SSAF (below) | todo |
| 7 | Comment pass: drop history/what-only comments, fix wrong ones, ASCII only | todo |

`nullsafe-upstream` keeps its name: it is the head of llvm PR #189131, and
GitHub cannot retarget a PR's head branch.

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
after, explain every sqlite diff line in the commit message, commit, update
this table. Tell the agent: "continue docs/nullability-safety-plan.md". The
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
| Whole program | `DerivedAnalysis` over `PointerFlowAnalysisResult` + `TypeConstrainedPointersAnalysisResult` + our summary result (template: `UnsafeBufferReachableAnalysis`) | Seed with nullable evidence, propagate along pointer-flow edges; nonnull = nonnull evidence with no nullable reaching it. Verify edge direction first: nullability flows with values, buffer bounds against them |
| Output | `SourceTransformation` -> `clang-apply-replacements` YAML + SARIF (template: `CppBoundedBuffers.cpp`) | Insert `_Nullable` / `_Nonnull` at each entity's declaration |

End-to-end lit pattern: `clang/test/Analysis/Scalable/source-edit-generation/cpp-bounded-buffers-replacements.cpp`
(extract -> `clang-ssaf-linker` -> `clang-ssaf-analyzer` -> transform).
