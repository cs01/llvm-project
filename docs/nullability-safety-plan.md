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
2. Lit: every `clang/test/*/nullability-safety*`,
   `clang/test/SemaCXX/nullability-default*` and
   `clang/test/Analysis/Scalable/NullabilitySafety` test passes.
3. sqlite differential (`SQLITE`, default `~/git/sqlite/sqlite3.c`; build it
   with `./configure && make sqlite3.c`): sorted warning lists in
   `-fnullability-default=nonnull` and `=nullable` modes, the decoded
   NullabilitySafety SSAF evidence (`Inputs/decode-summary.py`), and the
   lines the `nullability-annotations` transformation changes in a copy of
   sqlite3.c (the annotated copy must compile). Every gained or lost line is explained in this file
   (commit messages are one line). Counts depend on the sqlite version and
   platform headers, so compare runs on one machine only. Reference counts
   after step 2 (macOS, `build-arm`): nonnull 131, nullable 22849, evidence
   19620. After the rebase (Linux devserver, `build`, node's vendored sqlite
   3.53.1 at `~/git/node/deps/sqlite/sqlite3.c`): nonnull 139, nullable
   22642, evidence 19385. After 5d (same machine; evidence now counts
   decoded summary lines, not remarks): nonnull 107, nullable 22642,
   evidence 15367, annotations 2617.
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
| F1b | libstdc++ `shared_ptr`: `s->` / `*s` resolve to the base class `__shared_ptr_access`, whose type is not a smart pointer, so no dereference is checked (both modes, predates F1; libc++ and `unique_ptr` are fine) | done (below) |
| W1 | Wording: warnings name the internal `_Null_unspecified` tag in nullable mode, and an unnamed nonnull parameter prints as `''` | done (below) |
| F4 | Output parameters: a pointer escaping as `&p` to `T **` or binding to `T *&` / `const T *&` loses its nullable facts and guards (reuse `invalidateBoolGuardsFor` / `invalidateMembersFor`); narrowing is kept | done (below) |
| F5 | Lambdas: drop the call-site `IsLambdaCall` nonnull promotion; argument check only for `_Nonnull` or `nonnull(N)` (first parameter is `nonnull(2)`) | done, nonnull mode only (below) |
| 5b | SSAF extractor alongside the remarks; parity check: every remark has a matching summary entry on sqlite; argument evidence assumes callee contracts (below) | done (below) |
| 5c | SSAF whole-program propagation (below) | done; Nonnull is a must-property since the must-nonnull step (below) |
| 5d | SSAF source transformation; then delete the remarks and the remark-scraping loop | done; the `handle*Evidence` callbacks stay, the extractor needs them (below) |
| F2a | Ternary implications: reverse direction, pointer/comparison/conjunction antecedents, transitive narrowing via worklist | done (below) |
| 7 | Comment pass: drop history/what-only comments, fix wrong ones, ASCII only | done for `NullabilitySafety.cpp` (moved two doc comments that sat on the wrong function, dropped a stale 'local-var sources only' note and history references); SSAF files checked |
| S8 | Smart pointers: a class deriving from a std smart pointer is checked like one (F1b stopped checking a class deriving from `unique_ptr`) | done (below) |
| S4 | `!(sp != nullptr)` narrows when `!=` is a C++20 rewritten comparison (libc++ declares only `operator==`), as in assertion macro expansions | done (below) |
| S5 | `_Nonnull` / `_Nullable` on a reference to a smart pointer is read from the referenced type, for the dereference check and for the unspecified-mode opt-in; a local initialized from a `_Nonnull`-returning call is non-null | done (below) |
| S0 | A declaration drops every fact about its variable (found while validating S2: facts from the previous loop iteration survived the next iteration's declaration) | done (below) |
| S2 | A smart pointer reached through a reference (parameter, reference member) is tracked: null checks narrow it, and `reset()`, assignment and `std::move` through it drop the narrowing | done (below) |
| S3c | A guard's facts about a smart pointer are dropped when the pointer is assigned, reset, released, swapped or moved from | done (below) |
| S1 | A smart pointer dereference narrows the pointer for the rest of the path, so only the first dereference on each path warns (raw pointers unchanged) | done (below) |
| S6 | `std::static_pointer_cast` / `const_pointer_cast` / `reinterpret_pointer_cast` of a non-null smart pointer is non-null (the rvalue overloads move the source); `std::dynamic_pointer_cast` may yield null under either default | done (below) |
| S3a | A guard built from a ternary with one constant false arm narrows (`p ? p->n : 0`, `p == nullptr ? false : X`) | done (below) |
| S3b | A guard stored in a field (`d.ok = p != nullptr`, `this->ok`) narrows like a guard variable; assigning a struct as a whole drops every fact about paths under it | done (below) |
| S9 | Smart pointer stores classify the value like raw pointer stores: a copy or move carries the source's nullability, and under the nonnull default an unannotated value stored in a member path is trusted, as in a local (found validating S7a) | done (below) |
| S7a | A member reached through a smart pointer's `->` or `*` (`p->child`, `p->raw`) is a member path rooted at `p`: checked, narrowed, and forgotten when `p` changes | done (below) |
| S7b | A dereference of a smart pointer returned by a call or `operator[]` warns when the result may be null by contract (`_Nullable` return, `std::dynamic_pointer_cast`); the narrow rule, pending a decision on unannotated returns | done (below) |

## Step 5b results

- `runNullabilitySafetyOnTU` (callees-first call-graph walk, all-returns-nonnull
  bookkeeping, no inference inside recursive cycles), `getNullabilitySafetyDefinition`
  and `hasExplicitNullabilityAnnotations` moved from Sema into
  `lib/Analysis/NullabilitySafety.cpp`; Sema and the extractor share them.
  No behavior change (gates identical).
- `clang/{include,lib}/ScalableStaticAnalysis/Analyses/NullabilitySafety/`:
  `NullabilitySafetyEntitySummary` (per contributor: `NonnullEvidence`,
  `NullableEvidence`, `AllReturnsNonnull`, each an `EntityPointerLevelSet` of
  parameters, fields and function returns), the extractor
  (`--ssaf-extract-summaries=NullabilitySafety`; opt-in and options read
  from `LangOpts`, does not need `-fnullability-safety`; evidence inside a
  block is attributed to the enclosing function), JSON format, anchors.
- Parity: `tools/nullability-ssaf-parity.py` resolves each remark to a
  summary entity (see its docstring for the matching rule) and is a gate.
  sqlite, nonnull default: 4984 remark targets, 4904 summary entries, 0
  missing, 0 extra; 27 remarks name system-header functions that
  `EntitySourceLocations` does not locate, matching the 27 unlocated
  summary entries.
- Lit: `Analysis/Scalable/NullabilitySafety/{extraction.c,
  tu-summary-serialization.test}`, in the gates' lit set.
- `tools/sync-upstream.sh` now carries `clang/{include,lib}/ScalableStaticAnalysis/`
  so the extractor travels with its tests; since 5d the remarks are gone, so
  it is required. Run `git fetch llvm` first:
  with a stale `llvm/main` the merge base is old and the allowlist sweeps in
  unrelated upstream files.

## Step 5c results

`NullabilitySafetyAnalysis.{h,cpp}`: `NullabilityEvidenceAnalysisResult`
(union of all contributors' summaries) and `NullabilityInferenceAnalysisResult`
(`-a NullabilityInferenceAnalysisResult`, depends on `PointerFlow` too).
Nullable evidence is propagated along reversed pointer-flow edges (assignee
<- value), as designed, but the result is stricter than "seed and
propagate": the pointer-flow graph is flow-insensitive, so `if (p) s->x = p;`
is an edge `S::x <- p` and propagating would infer `_Nullable` for `S::x`
from a caller's nullable `p`, then warn on every dereference of `S::x`. So
propagation only vetoes:

- `Nullable` = direct nullable evidence (already narrowing-aware per TU)
- `Nonnull` = nonnull or all-returns-nonnull evidence not reached by any
  nullable value
- `NullableReachable` = everything propagation reached, for 5d to report
  (SARIF) without annotating

sqlite (one TU, nonnull default): evidence nonnull 4061, all-returns 187,
nullable 656; inferred `Nonnull` 1364, `Nullable` 656, `NullableReachable`
6471 (locals included). The veto removes about two thirds of the nonnull
candidates. Lit: `Analysis/Scalable/NullabilitySafety/propagation.c` (two
TUs, link, analyze; a VETO check verified to fail without the veto). The
gates now also build `clang-ssaf-linker` and `clang-ssaf-analyzer`.

## Must-nonnull inference

Review finding (reproduced): `s->x = &v; s->x = mystery();` inferred and
wrote `_Nonnull` for `S::x`, because a store the flow analysis could not
classify left no evidence and inference needed only one nonnull observation
plus no nullable reachability. Nonnull is now a must-property:

- `NullabilityEvidence::Unknown`: `classifyEvidence` always returns a kind.
  The extractor turns an Unknown store into `ConditionalEvidence` (assignee
  <- the entities `translateDeclPointerLevel` finds in the value) or, when
  the value names no entity, `UnknownEvidence` (a hard veto).
- Inference: candidates are nonnull, all-returns-nonnull and conditional
  assignees; minus nullable-reachable and unknown; then the least fixpoint
  over the conditional dependencies (a copy cycle with no proven store is not
  inferred). The existential nonnull propagation along pointer-flow edges is
  gone; edges only spread the nullable veto.
- Virtual methods: their pointer parameters are Unknown (overriders are
  called through the base, where the evidence names the base's parameter).
- Aggregate initializers report their stores against the whole init list:
  non-Nonnull kinds veto (Unknown is opaque), Nonnull is not reported, so an
  aggregate never makes a field a candidate. Without this sqlite gained 31
  public API fields (`sqlite3_vfs::xDlOpen`, ...) from its own static
  tables, which user code outside the link unit also fills in.

sqlite: inferred Nonnull 842 -> 528, none gained. Lost: 262 whose stores
copy an unproven entity (mostly a caller passing its own parameter or a call
result), 51 locals (never annotated), 1 opaque unknown store. Annotated lines
1026 -> 663; warnings unchanged. Evidence lines grow with the new kinds.

Null-test veto through copy chains (also from the review): `collectVetoes`
followed one copy (`a = p; if (!a)`); `a = p; b = a; if (!b)` now vetoes `p`
too (local-to-local copies, resolved with a visited set). sqlite: 29 new veto
lines, mostly list links (`pNext = pIter->pNext; pIter = pNext` in a loop
testing `pIter`); inferred Nonnull 528 -> 521, none gained.

Still open (from the same review): callers outside the link unit (a
non-static function's parameters assume the link unit is the whole program),
indirect calls beyond the address-taken veto, fields written through zeroed
allocations or `memset`, ObjC evidence, and multi-TU tests for libraries.
Annotation output stays experimental.

## Step 5d results

- Review follow-up (before the transformation): the opt-in rule is one
  function, `isNullabilitySafetyOptedIn`, applied inside
  `runNullabilitySafetyOnTU` (Sema's predicate delegates to it); the
  caller's filter only skips system headers. The stateful `ShouldAnalyze` /
  `AfterFunction` pair became handler hooks `startFunction` /
  `finishFunction` (Sema flushes diagnostics, the extractor sets its
  contributor). sqlite 0 / 0 on every list, parity 0 / 0.
- C++ contributors (`extraction.cpp`): method, constructor initializer,
  lambda body, template instantiation and block all land in a summary.
  Objective-C methods do not: SSAF's `getEntityName` has no entities for
  `ObjCMethodDecl`, so evidence observed inside an ObjC method body has no
  contributor and is dropped. The remarks did report it, so deleting them
  loses ObjC evidence until SSAF grows ObjC entities (upstream work).
- Extraction tests decode summaries with `Inputs/decode-summary.py`
  (`<contributor> <set> <entity>` lines): SSAF entity ids follow `Decl *`
  hash order and reshuffled after an unrelated change, so no test pins ids.
- `nullability-annotations` transformation
  (`SourceTransformation/Transformations/NullabilityAnnotations.cpp`):
  inserts the keyword after the outermost `*` of parameters, fields and
  returns (covers `T **`, `int (*p)`, function pointers, trailing return
  types), or after a typedef name. Skips silently when the type already
  carries `_Nonnull` / `_Nullable` or a nullability keyword follows the
  `*` (a written `_Null_unspecified`). Reports to SARIF: macro spelling,
  unrewritable types (`auto`, arrays, references), inner-level-only
  inferences, and `NullableReachable` entities. Per-TU edits must go
  through `clang-ssaf-src-edit-merge` (otherwise a header edited by two TUs
  gets the keyword twice); `--ssaf-link-unit-id` must be the linker output
  file stem. Lit: `annotations.cpp` (two TUs, shared header, compiles with
  `-Werror` afterwards).
- Remarks deleted: diagnostics, `-Rnullsafe-evidence` group, Sema
  overrides, `tools/nullability-ssaf-parity.py`. The seven remark tests:
  `warnings-off` deleted (the extractor does not depend on warning flags),
  `ctor-init` and `fixpoint` moved to `Analysis/Scalable/NullabilitySafety`,
  the other four keep `-verify` for their warnings and check decoded
  evidence. Every old remark maps to a summary line except per-site
  duplicates (summaries are sets). `fixpoint` gained `loop_return_only`:
  summaries cannot show two polarities for one return site, but a spurious
  nonnull from an unconverged visit still shows there.
- Unnamed parameters now get evidence (the skip existed because a remark
  needed a name; SSAF keys parameters by index). sqlite evidence 0 lost /
  1570 gained, all parameters of unnamed prototypes; lit
  `nullability-safety-dynamic-cast.cpp` gains `takesNonnull`'s evidence.
  Annotations 21 lost / 141 gained (the 21 are lines that gained a second
  keyword).
- Fields are never written `_Nullable`. Applying every annotation to
  sqlite raised nonnull-mode warnings from 107 to 2668; 170 of the
  `_Nullable` lines were fields such as `Vdbe::aOp` (64 new warnings) and
  `Table::aCol` (137), null only in a lifecycle state (before setup, after
  teardown) and dereferenced under invariants the analysis cannot see. Of
  187 fields with nullable evidence only 91 also had nonnull evidence, so a
  "contested" rule would miss half (for example `BtShared::pPage1`). Now a
  field inferred `_Nullable` keeps its default and each store of null into
  it is reported (`nullability-null-store-to-field`, 357 on sqlite);
  the checker itself does not warn on those stores unless the field is
  declared `_Nonnull`. Annotated sqlite: 447 warnings.
- Parameter `_Nullable` had the same problem. Of about eight sampled
  warnings none was a real bug: pointer / `rc` correlations (`vdbeCommit`,
  `pager_delsuper`), pointer / flag correlations (`getIntArg`,
  `sqlite3Reindex`, `checkTreePage`), a grammar invariant
  (`sqlite3Analyze`), a guard in a helper (`sqlite3_step`), an API
  contract (`sqlite3_vtab_collation`). Two changes:
  - Three-way evidence (`NullabilityEvidence`: Nonnull, Nullable,
    MaybeNull). `NullState` keeps must-nullable sets next to the may sets
    (intersected at joins); a value is Nullable evidence only when null on
    every incoming path, MaybeNull when null on some path only, and a
    ternary with a literal null arm is MaybeNull. MaybeNull
    (`MaybeNullEvidence` in the summary) seeds the veto propagation but
    never produces `_Nullable`. Warnings unaffected (they use the may sets).
    Dropping may-null evidence outright was tried first and is wrong: it
    lifted the veto and inferred `BtShared::pLock` (a list head)
    `_Nonnull`. The null-arm rule removed a wrong `_Nonnull` on
    `sqlite3ErrorWithMsg`'s `zFormat` (callers pass `x ? "%s" : 0`).
  - The transformation writes `_Nonnull` only; inferred `_Nullable`
    parameters and returns are SARIF suggestions
    (`nullability-nullable-suggestion`).
  sqlite: nonnull / nullable warnings 0 / 0; evidence 15418 lines (639
  Nullable-inferred entities become suggestions, 330 null stores to fields
  reported); annotated copy: 107 warnings, identical to the unannotated
  baseline.
- Decoded-summary tests start with `CHECK-NOT: {{.}}`: without it FileCheck
  skips unexpected lines before the first match.

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

## F1b results

`isSmartPointerObject` looks through implicit derived-to-base casts before
the type test at every receiver site (`->`, `*`, `get()`, `operator bool`,
`reset`, `release`, member and `std::swap`). libstdc++ declares those on
`__shared_ptr_access` / `__shared_ptr`, so before this `s->x` was never
checked, `s.reset(); s->x` was silent, and `if (s) { S *p = s.get(); p->x; }`
warned. Also new, for every smart pointer: `if (sp.get())`,
`sp.get() != nullptr` and their negations narrow `sp` (`smartPtrGetRef`);
`unique_ptr` had the same false positive. Checked against real libstdc++
`<memory>`. sqlite: no change (C).

## W1 results

Dereference and arithmetic warnings strip `_Null_unspecified` from every
pointer level of the printed type (a tag the user wrote, like `_Nullable`,
stays); qualifiers are kept. An unnamed nonnull parameter prints as
`parameter 1`. Clang's own diagnostics (`nullability-safety-default-tagging.c`)
and function-pointer parameter lists still show the tag. sqlite nullable:
22335 lines change text only; with the tag and pointer spacing normalized the
lists are identical. Other lists unchanged.

## F2a results

`recordPointerImplication`: `q = c ? E : nullptr` (or `c ? nullptr : E`)
stores the facts of `c` taking the non-null arm in `BoolGuards[q]`; `&&`
conjuncts (or `||` disjuncts under a null true arm) each contribute, and `E`
need not be provably non-null. `applyNarrowing` follows these entries with a
worklist and a visited set. Reusing `BoolGuards` keeps the existing join
(intersection) and invalidation: reassigning either side, a store through a
tracked `T **`, or an escape drops the entry (`forgetFactsAbout` now also
erases a pointer's own key). Facts naming the assigned pointer itself, or a
variable the ternary mutates, are dropped. sqlite nullable: one false positive
gone, `sqlite3DbStrNDup` (`zNew = z ? ... : 0; if (zNew) memcpy(zNew, z, n)`).
Other lists unchanged.

## GAP 1 results

`reportNonnullMembersNullAtExit`: after the reporting pass, a `_Nonnull`
smart pointer member of `this` that is must-nullable in the exit block's
state (reset, released, or moved from on every path) is reported once at the
closing brace (`warn_nullability_safety_member_exit`, assignment group).
Destructors, `&&`-qualified methods, static methods and lambdas are exempt;
a member nulled on only some paths is not reported. Raw pointer members are
not reported at exit: the assignment that nulls one already warns. Checked
with real libstdc++ `unique_ptr` and `shared_ptr` in both modes. sqlite: no
change (C).

## Return-type consistency results

Clang warned on conflicting parameter nullability across redeclarations but
not on return types, and not at all across C++ overrides.
`checkReturnNullabilityRedecl` (in `MergeFunctionDecl`, next to
`mergeParamDeclTypes`) reuses `warn_mismatched_nullability_attr`;
`checkOverridingNullability` reuses the ObjC override diagnostics and fires
only when an override weakens the base: a `_Nullable` return for a
`_Nonnull` one, or a `_Nonnull` parameter for a `_Nullable` one. Both need
explicit specifiers on both sides and run without `-fnullability-safety`
(group `-Wnullability`, like the parameter check). Clang's Sema, SemaCXX,
SemaObjC(XX), SemaTemplate, APINotes, Modules, Analysis and Index suites
pass (6583 tests). sqlite: no change.

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

## Smart-pointer track (S steps)

From a review of smart-pointer dereference warnings on real C++ code built
against libc++ in C++20 mode (where `shared_ptr` declares only
`operator==(const shared_ptr &, nullptr_t)`, so `!=` and reversed
comparisons are rewritten) and against libstdc++ (inherited operators). Each
defect has a target-behavior test; numbers follow the review, and the steps
land in dependency order: S8, S4, S5, S0, S2, S3c, S1, S6, S3a, S3b, S9, S7a,
S7b (S0 and S9 were found while validating S2 and S7a and are not in the
review).
libc++-shaped cases live in `SemaCXX/nullability-safety-smart-ptr-libcxx.cpp`
(added with S4),
libstdc++-shaped ones in `nullability-safety-smart-ptr-base-access.cpp`.
Nonnull-mode expectations follow F1: an unannotated, unchecked smart pointer
is trusted there, so only flow-tainted or `_Nullable` ones warn.

Per step, besides the gates (sqlite 3.50.4 amalgamation, Linux devserver;
reference counts nonnull 119, nullable 22282, evidence 19712, annotations
637): each test is replayed against real libstdc++ and libc++ `<memory>`, and
an LLVM differential runs `-fsyntax-only` in both modes over 17
smart-pointer-heavy LLVM and clang TUs (`VirtualFileSystem.cpp`, Orc `Core.cpp`,
`LLJIT.cpp`, `CompilerInstance.cpp`, `ASTUnit.cpp`, `Driver.cpp`, ...) with
their build flags and system libstdc++ (reference counts: nonnull 33,
nullable 4653).

## S8 results

`isSmartPointerObject` accepts the object when its type, or a base class on
the path of a derived-to-base cast it goes through, is a std smart pointer.
F1b tested only the type under the implicit casts, which for
`struct D : std::unique_ptr<T> {}` is `D`, so `d->x` stopped being checked
and `if (!d)` stopped narrowing. Testing the type at the call instead would
lose libstdc++'s `shared_ptr` again, and neither covers a class deriving from
libstdc++'s `shared_ptr`, whose object is cast straight to
`__shared_ptr_access` with `shared_ptr` only on the cast's path. Tests in
`smartptr-parity.cpp` (unique_ptr) and `smart-ptr-base-access.cpp`
(libstdc++ shape), verified to fail before the change; checked against real
libstdc++ and libc++ `<memory>`. sqlite: no change. LLVM differential: no
change.

## S4 results

`analyzeCondition` peels `!`, `__builtin_expect`, explicit casts to `bool`
and `CXXRewrittenBinaryOperator` in any order. It used to unwrap a rewritten
comparison once, before the `!` loop, so in `!(sp != nullptr)` (and
`!!(sp != nullptr)`, `__builtin_expect(!!(!(sp != nullptr)), 0)`, a guard
`bool missing = !(sp != nullptr)`) the loop stopped at the rewritten
operator and recorded nothing: with libc++ in C++20 the semantic form of
`sp != nullptr` is `!(sp == nullptr)`. `!__builtin_expect(c, 1)` also
narrows now (the builtin was only looked through outermost). New test file
`smart-ptr-libcxx.cpp`, verified to fail before the change and to pass with
real libstdc++ and libc++ `<memory>`. sqlite: no change. LLVM differential:
no change (it builds as C++17).

## S5 results

For `const std::shared_ptr<T> _Nonnull &p` the annotation is on the
referenced type, so `isNonnullType(VD->getType())` saw none: `p->x` warned
although the contract was written, and without `-fnullability-default` a
function annotated only through a reference parameter or return type was not
analyzed at all (`_Nullable &` dereferences were silent).
`getSmartPointerDeclaredType` strips the reference (variables and fields),
and a local reference without its own annotation reads its referent's, as
its flow facts already do; the opt-in rule reads parameter and return types
through `declaresNullability`, which strips references (functions, ObjC
methods, blocks). `isNonnullSmartPtrInit` also accepts a call whose
callee's declared return type is `_Nonnull`: `auto q = f()` inherited the
annotation through deduction, `std::shared_ptr<T> q = f()` and
`const std::shared_ptr<T> &r = g()` did not. New test
`smart-ptr-nonnull-ref.cpp` (nullable, nonnull and unspecified RUN lines),
verified to fail before the change and to pass with real libstdc++ and
libc++ `<memory>`. sqlite: no change (C). LLVM differential: no change.

Not changed, noted for a decision: in unspecified mode an opted-in function
still warns on an unannotated, unchecked smart pointer (the mode default is
not `nonnull`, so `isSmartPointerMaybeNull` falls through to "may be null"),
while an unannotated raw pointer there is trusted.

## S0 results

`VisitDeclStmt` forgets the variable (`forgetVariable`: its narrowed,
nullable and must-nullable flags, and via `forgetFactsAbout` the member
paths rooted at it, guards naming it or keyed on it, aliases and address-of
records) before classifying the initializer, for every variable with local
storage. A declaration runs once per loop iteration and makes a new object
or binds a reference anew, but initialization, unlike assignment, never
dropped the old facts: `p = nullptr` or `sink(std::move(p))` at the end of a
loop body reached the next iteration's `T *p = f()` / `auto p = f()` through
the back edge (nullable facts union at joins), and so did facts about paths
under the old pointer (`pExpr->x.pList = 0`). Static locals keep their value
and are not forgotten. Found validating S2, which lets `std::move(q)` taint
a range-for reference `auto &q`; on the LLVM differential that added 8
warnings in nonnull mode, all this pattern. Tests in `smart-ptr-libcxx.cpp`
and `default-nonnull.cpp` (raw pointer), verified to fail before.

sqlite nonnull: 1 lost, 0 gained: `sqlite3ExprListToValues`
(`pExpr->x.pList->nExpr` after the previous iteration's
`pExpr->x.pList = 0`, a different `Expr`). Nullable: no change. Evidence: 2
`MaybeNullEvidence` lines become `ConditionalEvidence`, the argument of
`sqlite3DbFree(db, pDb->zDbSName)` in `sqlite3CollapseDatabaseArray`
(`pDb->zDbSName = 0; continue;` on the previous element) and of
`sqlite3SelectNew(pParse, pExpr->x.pList, ...)` in
`sqlite3ExprListToValues`: both were judged from a stale fact about another
array element. Annotations: no change. LLVM differential: nonnull 33 -> 23,
0 gained (8 in `BugReporter.cpp`, 1 each in Orc `Core.cpp` and
`ExecutionUtils.cpp`: smart pointers declared in a loop and moved from at
its end); nullable no change.

## S2 results

`smartPtrRef` judges the expression (`isSmartPointerObject`) instead of the
declared type: for `const std::shared_ptr<T> &p` or a reference member the
declaration is a reference type, which `isSmartPointerType` rejects, so
`p == nullptr` did not narrow (`!p` did, through `operator bool`), a
reference member was never checked, and `reset()`, assignment and
`std::move` through a reference did not drop a narrowing. Local references
bound to a variable were already resolved to it. `analyzeSmartPtrNullCompare`,
`operator bool`, `get()` and the dereference report go through the same
test, which also lets `==` / `!=` and `reset()` reach a class deriving from
a std smart pointer (S8). A call is still not assumed to change what a
reference refers to (as for member paths; locked by `s2_ref_across_call`).
Tests in `smart-ptr-libcxx.cpp` and `smart-ptr-nonnull-ref.cpp`, verified to
fail before and to pass with real libstdc++ and libc++ `<memory>`. sqlite:
no change (C). LLVM differential: nonnull no change; nullable 8 lost, 0
gained, all `auto &x = map[k]; if (!x) x = std::make_*(...);` on a reference
(`ASTUnit.cpp` x5, `Driver.cpp`, Orc `Core.cpp`), and one call now summarized
all-returns-nonnull (`getBugTypeForName` returns `.get()` of such a
reference, `BugReporter.cpp`).

## S3c results

`bool ok = sp != nullptr; sp = f(); if (ok) sp->x` did not warn: raw
pointer assignment drops the guards naming the pointer through
`forgetFactsAbout`, but the smart pointer handlers never did.
`forgetSmartPtrFacts` (a variable: `forgetFactsAbout`; a member path:
`invalidateGuardsAndAliasesWithPrefix`) now runs for the assigned pointer and
a moved-from source in `handleSmartPtrAssign`, the moved-from source of a
move construction, `reset()`, `release()`, member and `std::swap` (both
sides) and a bare `std::move`. `sp = sp` returns early like `p = p`, so its
guards survive; before, it only kept the narrowing. Tests in
`smart-ptr-libcxx.cpp`, verified to fail before (and the self-assignment
case to fail without the early return). sqlite: no change (C). LLVM
differential: no change.

## S1 results

`checkSmartPtrDeref` marks the dereferenced smart pointer narrowed after the
check: if `sp->x` did not crash, `sp` is non-null for the rest of the path,
so every later `sp->` / `*sp` repeated the same finding. The join keeps the
fact only when every incoming path dereferenced; assignment, `reset()` and a
move drop it, also through a reference (S2). Raw pointers are unchanged (a
decision, not a limitation: the same one line in `checkVarDeref` would do
it; locked by `s1_raw`). The narrowing also counts as proof for a later
`sp.get()`: `S *f(std::shared_ptr<S> p) { p->x; return p.get(); }` is
all-returns-nonnull. Tests in `smart-ptr-libcxx.cpp`, verified to fail
before. No existing expectation changed. sqlite: no change (C). LLVM
differential: nullable 4645 -> 4440, nonnull 23 -> 9, 0 gained anywhere; each
lost line is a repeat of a dereference that still warns earlier on its path
(for example `Interpreter.cpp:363` stays, 369-394 go; `LLJIT.cpp:1020` stays,
1027-1053 go).

The two nonnull warnings left at `Interpreter.cpp:363` were a member
assigned an unannotated value (`CI = std::move(Instance)`,
`Act = TSCtx->withContextDo(...)`), removed by S9.

## S6 results

Copies of a narrowed smart pointer were already narrowed (and are from a
reference since S2). `lookThroughNullPreservingConversions` now also looks
through `std::static_pointer_cast`, `const_pointer_cast` and
`reinterpret_pointer_cast`, whose result is null exactly when the argument
is: for copy sources, for `isNonnullSmartPtrInit` / `isNullSmartPtrInit`
(`static_pointer_cast<B>(std::make_shared<D>())`), and for a move through
the cast. The C++20 rvalue overloads move the source into the result, so
`isStdMoveInsideSmartPtrTransferCtx` walks up through such a cast: the result
inherits the source's narrowing and the source is moved-from, as for
`auto q = std::move(p)`. `std::dynamic_pointer_cast` is excluded, and its
result is treated as a `_Nullable` return (`isNullSmartPtrInit`): it yields
null when the runtime check fails, like a raw `dynamic_cast`, which already
warns under both defaults. This adds nonnull-mode warnings for unchecked
`dynamic_pointer_cast` results (none in the gates' code). Tests in
`smart-ptr-libcxx.cpp` (the mock gains converting constructors,
`make_shared` and the four casts with both overloads, as libc++ declares
them), verified to fail before and to pass with real libstdc++ and libc++
`<memory>`. sqlite: no change (C). LLVM differential: no change.

A copy then only inherited narrowing, never nullability; S9 changes that.

## S3a results

`computeGuardFacts` accepted a ternary only when both arms were constants.
With exactly one constant false arm, `c ? X : false` holds exactly when
`c && X` does and `c ? false : X` when `!c && X` does, so the guard being
true proves the facts of `c` (of `!c`: the false-direction facts of a `||`
chain) and those of `X`; as for `&&`, only guard-true facts are kept, and a
constant true arm (`c || X`) proves nothing when the guard is true. Tests in
`smart-ptr-libcxx.cpp` and `Sema/nullability-safety-guard-idioms.c`,
verified to fail before. sqlite nullable: 5 lost, 0 gained, all this shape:
`n = pList ? pList->nExpr : 0; if (n == 2) pList->a[1]` (`resolveExprStep`),
`nArg = pExpr->x.pList ? pExpr->x.pList->nExpr : 0; ... nArg == 1`
(`analyzeAggregate`), and `hasDistinct = pDistinct ?
pDistinct->eTnctType : WHERE_DISTINCT_NOOP; if (hasDistinct)` (three lines
in `selectInnerLoop`). Other lists and the LLVM differential: no change.

## S3b results

`BoolGuards` is keyed by `PtrRef` (a variable or a member path) instead of
`VarDecl`. Assigning an integer field records the facts of its value
(`handleMemberAssign`), `analyzeCondition` and the `flag == constant` form in
`analyzeNullCompare` look a field guard up by its path, and a field guard is
dropped with the path: writing the field or a prefix of it
(`invalidateGuardsAndAliasesWithPrefix` now also removes keys under the
prefix), `++` / `--` on it, reassigning the variable it is rooted at
(`invalidateMembersFor`), and changing a pointer its facts name (unchanged:
that looks at values). Assigning a struct as a whole (`d = other`; a
`BinaryOperator` in C, `operator=` in C++) dropped nothing before, not even
member-path narrowing (`if (d.p) { d = other; *d.p; }` was silent); it now
runs `invalidateMembersFor`. As for member paths, a call is not assumed to
change a field guard. Tests in `smart-ptr-libcxx.cpp` and
`Sema/nullability-safety-guard-idioms.c`, verified to fail before. sqlite:
no change. LLVM differential: no change.

## S9 results

Found validating S7a, which makes paths through smart pointers member paths
(`AST->ModCache = f(); *AST->ModCache`) and gained two nonnull-mode LLVM
warnings of one shape: `handleSmartPtrAssign` marked a member path nullable
before looking at the value, so under the nonnull default
`m = make(); m->x` warned where the same code on a local, and a raw member
(`storePointer` leaves an unknown value unknown), did not. Now:

- A member path assigned a value the transfer cannot classify is cleared,
  like a local, under the nonnull default. Under the other defaults it is
  still marked nullable: a `this->` smart pointer member is trusted unless
  flow marks it (`warnSmartPtrDeref`), where a raw member would fall back to
  its `_Null_unspecified` type.
- Dropping that mark alone would lose `m = std::move(moved_from)`, so a
  copy or move now carries the source's nullability as well as its
  narrowing (`isSmartPointerKnownNullable`: flow-nullable, or `_Nullable`
  and not narrowed), in initialization and assignment. This also makes
  `std::shared_ptr<T> q = p;` nullable for a `_Nullable` `p` under the
  nonnull default (`auto q = p` was, through the deduced type), as
  `T *q = p` is.

Tests in `smart-ptr-libcxx.cpp`, verified to fail before (nullable mode is
unchanged by construction). sqlite: no change (C). LLVM differential:
nonnull 9 -> 2, 0 gained (all seven a member assigned an unannotated value:
`TrimmedGraph = OriginalGraph->trim(...)` in `BugReporter.cpp`, the two in
`Interpreter.cpp`, four `LLJIT.cpp` members such as `ES = std::move(S.ES)`);
nullable no change.

## S7a results

`decomposeMemberAccess` continues through a std smart pointer's `operator->`
/ `operator*` (`isSmartPointerObject`) as through `->` on a raw pointer, so
`p->child` and `(*p).child` are the path `{p, child}`; a local smart pointer
reference roots its paths at its referent (`resolveSmartPtrReference`), as
`PtrRef::fromExpr` does. Before, `PtrRef::fromExpr` failed at the operator
call: `p->child->x` was never checked, `!p->child` did not narrow, and
`checkMemberExprDeref` returned early on a smart pointer base, so raw members
reached through one (`p->raw->x`) were never checked either (that early
return also re-checked `p` at the wrong location; S1 had already made it
silent). Changing `p` drops the paths under it: a variable through
`forgetFactsAbout`, a member path (`this->sp->child` when `this->sp` is
assigned) through `forgetSmartPtrFacts`, which now also removes the paths
below it. Paths rooted at `this` keep their rule (`warnSmartPtrDeref`:
reported when flow marks them). Tests in `smart-ptr-libcxx.cpp`, verified to
fail before and to pass with real libstdc++ and libc++ `<memory>`. sqlite:
no change (C). LLVM differential: nonnull no change (the two nonnull
findings this step first gained were S9's member stores); nullable 14
gained, 0 lost, all unannotated members reached through a smart pointer,
judged by the nullable default as `s.m->x` on a struct variable already is:
`*AST->CodeGenOpts`, `*AST->ModCache`, `*AST->Consumer` (`ASTUnit.cpp`; the
last behind correlated `if`s the analysis does not relate), `*UMI->RT` and
`UMI->MU->...` (Orc `Core.cpp`, raw and smart members), `*NewModule->Buffer`
(`ModuleManager.cpp`), `Interp->TSCtx->...`, `*Interp->DeviceAct`
(`Interpreter.cpp`), and `TmpS->Ctx.get()` passed to a lambda parameter
(`ThreadSafeModule.h`, twice).

## S7b results

`lookupN()->x`, `(*lookupN()).x` and `m[k]->x` were never checked, even for
a `_Nullable` return: `warnSmartPtrDeref` gave up when `PtrRef::fromExpr`
found no variable or member path. A call result has no identity to narrow,
so the rule is the narrow one: it warns only when it may be null by
contract (`getNullableSmartPtrCallResult`): the callee's declared return
type is `_Nullable` (read from the declaration; overload resolution strips it
from the object expression), or the callee is `std::dynamic_pointer_cast`
(as `dynamic_cast<D *>(p)->x` already warns). An unannotated return is not
reported under either default. Decision pending: the broad rule (a
temporary warns exactly when a local bound to it would) would report every
unannotated `f()->x` under the nullable default. Tests in
`smart-ptr-libcxx.cpp` (functions, methods, `operator[]`, a function
template), verified to fail before and to pass with real libstdc++ and
libc++ `<memory>`. sqlite: no change (C). LLVM differential: no change (no
`_Nullable` smart pointer returns there).

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
playground (`nullsafe-playground/` -> `playground/`), release archives
(`clang-nullsafe-*` -> `clang-nullability-safety-*`) and benchmark scripts
were renamed later, and the curl installer was deleted; the `-Rnullsafe-evidence` remarks were
removed in 5d.

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
| Evidence | `-Rnullsafe-evidence` remarks | SSAF summaries (below; remarks removed in 5d) |
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
