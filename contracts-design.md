# Readable contracts for C in clang

Status: design draft, nothing implemented.
Revised: 2026-09-04, after an adversarial review that killed several claims in
the first draft. Corrections from that review are marked where they changed the
design, so the reasoning is not silently rewritten.
Base: branch `contracts-c-dev`, forked from upstream `main` 2fd31faf6ec5
(2026-09-04). Every in-tree citation below was checked against that commit.

## 1. Goal

Give C first-class contracts in clang: preconditions, postconditions, and frame
conditions that the compiler parses and checks, rather than macros that expand
to nothing or comments no tool reads. Useful in two ways from one source text:

- **checked at runtime**, turning silent corruption into a deterministic trap at
  the earliest wrong state,
- **checked statically at call sites**, so a caller that violates a callee's
  contract is a compile-time diagnostic.

Target: pure C. Evaluation target: production compression code (zstd, openzl)
and sqlite.

Explicitly **not** goals, each cut for a reason given below: proving whole
programs correct (section 8), an `assume` mode that feeds the optimizer
(section 3), and a bespoke SMT verification tier (section 7).

## 2. What contracts can and cannot guarantee

Three tiers get conflated. They are not the same tool and do not have the same
cost.

| Tier | Guarantee | Annotation cost | Precedent |
|---|---|---|---|
| Runtime-checked | None statically. Converts silent corruption into a deterministic trap. | Low | C++26 P2900, SAL, `assert` |
| Path-sensitive bug finding | Unsound and incomplete. Finds real bugs, proves nothing. | Low | clang static analyzer, PREfast |
| Deductive verification | Proves "precondition implies postcondition, no UB" for that function. | High | Frama-C/WP, VeriFast, VCC, CBMC |

Only the third tier proves anything, and it proves the code matches the spec,
not that the spec is right. A wrong `post` is proved happily.

**What is actually runtime-checkable is narrower than it looks.** A predicate
can be compiled into a branch only if the program has the information at hand:

- `p != NULL`, `n > 0`, `r <= cap`, comparisons between scalars and pointers:
  yes.
- `valid(p, n)`, meaning "p points to at least n readable bytes": **no.** There
  is no way to learn the allocation behind a `void *` at function entry. Clang's
  `-fexperimental-bounds-safety` (`Options.td:2109`) is the machinery that would
  answer this, and its own documentation says "Not fully implemented upstream"
  (`clang/docs/BoundsSafetyImplPlans.md:11`); `SemaBoundsSafety.cpp` is 415
  lines with no runtime checks.
- `writes(...)`: **no.** Checking a frame condition at runtime means shadow
  memory. E-ACSL had to build an entire runtime for this.

So under runtime checking, buffer clauses are documentation that the static
tiers consume, not traps. The first draft implied otherwise; it was wrong.

### Rust comparison, stated more carefully

Porting to Rust buys spatial and temporal memory safety, not functional
correctness: a decoder that mis-parses a header and emits wrong-but-in-bounds
bytes is a safe Rust program and a broken decompressor. Two honest caveats the
first draft skipped. First, most decoder CVEs *are* memory-safety bugs, and
round-trip fuzzing already catches much of the rest. Second, Rust is not
ignoring this axis: Kani, Verus, Creusot, Prusti, and `core`'s `#[requires]` /
`#[ensures]` from the Verify Rust Std work all exist. The C argument is that C
is not going anywhere, not that Rust has nothing here.

### The static analyzer is not an SMT solver, and its Z3 path is not usable

CSA is symbolic execution over an exploded graph with a `RangeConstraintManager`.
The first draft claimed the solver plumbing was in-tree and reusable. Checked,
and it is not:

- `clang/include/clang/StaticAnalyzer/Core/Analyses.def:21` spells the option
  `unsupported-z3`, described in-tree as "Known to crash; patches welcome, crash
  reports are not." There is no `-analyzer-constraints=z3`.
- `llvm/include/llvm/Support/SMTAPI.h:182-200` offers Bool, BitVector, and
  Float16/32/64/128 sorts. No array theory, no quantifiers, no uninterpreted
  functions. A block-based memory model needs arrays; range predicates need
  quantifiers. There is nothing there to encode into.

## 3. Recon: what exists in this tree

Checked against `2fd31faf6ec5`. The first draft's line numbers were carried over
from a November-2025 checkout and were wrong; these are re-verified.

- **No C or C++ contracts.** Every "contract" hit in `clang/lib/Parse` is
  `fp_contract`. P2900 is not implemented here.
- **`counted_by` / `counted_by_or_null`** at `Attr.td:2677`, with
  `clang/lib/Sema/SemaBoundsSafety.cpp` (415 lines). Precedent for adding C
  safety surface to clang, but far less finished than the first draft implied.
- **`CXXAssume`** at `Attr.td:1941`, lowering to `llvm.assume`. Note this is the
  C++ statement attribute, not `OMPAssume` at `Attr.td:4877`; the first draft
  conflated them.
- **`diagnose_if`** at `Attr.td:3779`. The closest existing thing to a
  compile-time precondition.
- **No GCC `access` attribute.** `grep 'GCC<"access">' Attr.td` returns nothing.
  GCC has had `access(write_only, 1, 2)` since GCC 10, which is `valid` plus
  `writes` for C, and it drives `-Wstringop-overflow`. Clang lacking it is a
  gap, and closing it is independently upstreamable. See section 10.
- **`clang/lib/Analysis/LifetimeSafety/`**: 5821 lines, introduced 2025-07-10
  (#142313), reorganized 2025-10-10 (#162474), wired into ordinary warnings via
  `AnalysisBasedWarnings.cpp`. A CFG dataflow pass that verifies function bodies
  against their annotations, which its own docs call "contracts". The first
  draft called this "~4k lines, new since May 2026" and leaned on its newness as
  evidence of a shift in what clang will accept. It is over a year old. The
  precedent is still the best structural one in the tree; the timing argument
  built on it was wrong.
- **RegionStore invalidation is per-cluster.**
  `clang/lib/StaticAnalyzer/Core/RegionStore.cpp:1228` does
  `B = B.removeCluster(baseR)`: all-or-nothing for a base region, with no
  sub-range granularity. `CallEvent.cpp` already preserves `const T*` pointees
  via `TK_PreserveContents`. This bounds what frame conditions can buy in phase
  3; see section 6.
- Driver options live at `clang/include/clang/Options/Options.td`.

## 4. What the language needs

Ordered by how much each is missed when absent.

1. **`pre` / `post`.** Table stakes.
2. **Result naming in `post`.**
3. **`old(e)` in `post`.** Scalars only. Required more often than it looks:
   codec code mutates parameters constantly (`src += 4`, `dstCap -= n`), so a
   `post` naming a by-value parameter is ambiguous between its entry and exit
   value. P2900 forbids naming non-const by-value parameters in `post` for
   exactly this reason. **Rule adopted here: a `post` predicate may name a
   by-value parameter only through `old()`.** The first draft's headline example
   (`post(r: r <= dstCap)`) violated this and was wrong as written.
4. **Frame conditions (`writes`).** What makes modular reasoning possible at
   all. Also the heaviest annotation burden, which is why section 7 tries to
   infer them.
5. **Behaviors, or some way to say "on success X, on error Y".** A single `post`
   conjunction cannot describe a function that fills `dst[0..r)` on success and
   leaves it garbage on error, which is every function in the target libraries.
   ACSL uses named behaviors. Without something equivalent, `writes` and `post`
   both lie on error paths. The first draft missed this entirely and it is a
   v1-blocking gap, not a nicety.
6. **Memory predicates**: `valid(p, n)`, `disjoint(a, n, b, m)`. Static-only per
   section 2.
7. **Purity**, so predicates cannot have side effects, and a way to mark a
   function usable in specs.
8. **Loop invariants and variants.** Deferred with the proof tier; see section 7.
9. **Quantifiers over ranges.** Deferred with the proof tier.

Out of scope for v1, unchanged: `struct` type invariants, contracts on function
pointers, separation-logic ownership. Section 10 records what the function
pointer exclusion costs.

## 5. Syntax

**Revised from the first draft.** The original proposed contextual keywords in
the declarator suffix (`size_t f(...) pre(...) post(...);`). That is dropped.

### Why the suffix syntax dies

The plan's own evaluation gate is to annotate the public headers of zstd,
openzl, and sqlite. Those headers are compiled by every compiler in existence.
`size_t ZSTD_decompress(...) pre(...)` is a syntax error in GCC, MSVC, and every
clang without this fork, so contracts on a public header would have to be
`ZSTD_PRE(...)` macros expanding to nothing elsewhere, which is exactly the
macro shape the syntax section rejected. "Part of the grammar" and "on zstd's
public header" cannot both hold.

The mechanical problems are real too, and were understated as "parser lookahead":

- `dst[0..dstCap]` does not lex. `0..dstCap` is a single pp-number; clang says
  `error: invalid suffix '.2' on floating constant`. Verified.
- Parameters are out of scope after the closing paren.
  `ParseDecl.cpp` exits `PrototypeScope` immediately after
  `ParseFunctionDeclarator` returns, so a suffix clause cannot name parameters.
  Contracts would have to be parsed inside `ParseFunctionDeclarator` next to the
  exception-spec, and stored in `DeclaratorChunk::FunctionTypeInfo`, meaning
  they get parsed on *every* function declarator (function pointers, abstract
  declarators, `sizeof` operands) with Sema rejecting all but the one case.
- If `pre` is a typedef, `int f(a) pre(a) { }` is already a valid K&R
  declaration.
- `for (...) invariant(x);` is already a valid call statement when `invariant`
  is a function, so loop clauses need name lookup in the parser.
- A macro named `pre`, `post`, or `valid` in any earlier header silently
  rewrites contracts with no diagnostic. sqlite has `int exists;` locals.
- clang-format and clangd would both need to learn the grammar.

### What replaces it

C23 attributes in the `clang` vendor namespace, with real predicate parsing:

```c
[[clang::pre(dst != NULL)]]
[[clang::pre(src != NULL)]]
[[clang::pre(valid(dst, dstCap))]]
[[clang::pre(valid(src, srcSize))]]
[[clang::pre(disjoint(dst, dstCap, src, srcSize))]]
[[clang::writes(dst, dstCap)]]
[[clang::post(r, r <= old(dstCap) || ZSTD_isError(r))]]
size_t ZSTD_decompress(void *dst, size_t dstCap,
                       const void *src, size_t srcSize);
```

Verified: an unknown attribute in a vendor namespace parses its balanced token
sequence and is ignored with `-Wunknown-attributes`, in both `-std=c23` and
`-std=c11`. So a header annotated this way compiles everywhere; only this fork
gives the tokens meaning. Pre-C23 compilers that reject `[[...]]` outright still
need a one-line macro, which is a far smaller concession than macro-wrapping
every predicate.

Note the range syntax is now a call, `writes(dst, dstCap)`, not `dst[0..dstCap]`.
This sidesteps the pp-number problem and needs no new grammar at all.

This is less pretty than the suffix form. It is also how `counted_by`,
`diagnose_if`, and `lifetimebound` already ship, it is the only spelling with
any upstream path, and it is a superset of GCC's `access` attribute. And the
evidence says the spelling was never the blocker: see section 8.

Rules unchanged from the first draft: contracts on the prototype, inherited by
the definition; clauses conjoin in source order; predicates must be pure. New:
because a prototype and its definition have distinct `ParmVarDecl`s, and sqlite's
header has 186 prototype lines with unnamed parameters, the predicate expression
must be rebound at the definition, and positional parameter references are
needed for the unnamed case. Both were missing from the first draft's phase 1.

## 6. Implementation plan

### Phase 0: recon
Done, section 3.

### Phase 1: front end (attributes)
Attribute definitions in `Attr.td` with delayed argument parsing so predicates
are parsed as expressions in the right scope; Sema for type checking, purity,
`old()` and result binding, prototype-to-definition rebinding, positional
parameter references, consistency diagnostics. Serialization, `ASTDumper`,
AST matchers.

Gate: lit tests for parse, sema, `-ast-dump`. No codegen.

### Phase 2: runtime checking
`-fcontract-semantic={ignore,check}`. `check` emits a branch plus a handler call
per checkable clause, reusing the ubsan handler plumbing. `pre` at callee entry,
`post` at every return, entry snapshot for `old()` on scalars. Non-checkable
clauses (`valid`, `writes`) compile to nothing and are consumed only by phase 3.

**`assume` is cut.** The first draft proposed an `assume` mode lowering to
`llvm.assume` and sold it as a performance story. P2900 has ignore, observe,
enforce, and quick-enforce, and deliberately has no assume, because an assumed
predicate that is false is UB and a wrong contract silently miscompiles. That
risk is unacceptable on its own, and compounds with section 7: an inferred
contract that a human rubber-stamps would turn a tolerated latent bug into
optimizer-exploited UB. The expected win was also overstated, since `llvm.assume`
of a call to a non-`const` function like `ZSTD_isError` is dropped
(`warn_assume_side_effects`, `DiagnosticSemaKinds.td:982`), and `valid`,
`disjoint`, and range predicates have no `llvm.assume` lowering at all.

An `observe` mode (diagnose, do not trap) should be added instead, because a
library built with `check` traps its consumers' pre-existing benign misuse
without their opt-in. See section 10.

Gate: zstd, openzl, and sqlite build clean with annotated public headers; test
suites green under `check`.

### Phase 3: call-site checking
A checker that, at a call site, checks the callee's `pre` against the current
state, assumes its `post` afterward, and uses `writes` to limit invalidation.

**Precision expectations, corrected.** The first draft claimed frame conditions
make CSA "strictly more precise", implying sub-object precision.
`RegionStore.cpp:1228` removes an entire cluster keyed by base region, so
`writes(dst, dstCap)` cannot do better than `writes(dst)` without new machinery,
and `const T*` pointee preservation already exists. What survives is control over
*which* clusters a call invalidates rather than precision within one. That is a
real gain, but for codec code where the whole decoder state hangs off a single
`ZSTD_DCtx *`, it collapses. **This must be measured on roughly 20 hand-annotated
zstd functions before any of phase 3 is built.** If the measurement says the gain
is small, phase 3 shrinks to call-site `pre` checking, which is still worth
having and does not depend on frame conditions at all.

Host: Sema CFG dataflow (like `LifetimeSafety`, ships as a warning, reaches
everyone) or a CSA checker (path-sensitive, opt-in). Question 5.

### Phase 3.5: inference
Section 7.

### Phase 4: deductive verification: cut, exported instead
The first draft proposed WP over the CFG into Z3 with a CBMC-style memory model.
Cut for two reasons.

First, the plumbing does not exist (section 2) and building array theory and
quantifier support into `SMTAPI` is not a side quest.

Second and more decisive, the target code is excluded by construction. A typed,
block-based model with single-dimension buffers and no cross-object arithmetic
rejects `MEM_read32`-style memcpy punning, `ZSTD_wildcopy` with deliberately
overlapping source and destination, sliding-window matches spanning
prefix/extDict/dictionary as three separate objects, 64-bit bit-reader
containers with unaligned loads, and sqlite's unions. Frama-C's WP Typed model
rejects the same code, and its byte-level model has been experimental for a
decade. What would verify is the scalar leaf layer, `openzl/shared/{bits,
overflow, varint}.h`, which the existing fuzzer already covers exhaustively.

**Instead: emit `__CPROVER_requires` / `__CPROVER_ensures` / `__CPROVER_assigns`
from the same AST and run CBMC.** CBMC already has function contracts, the
memory model, and a contract-checking mode, deployed on aws-c-common, FreeRTOS,
and s2n. If proofs are wanted, this is a translation layer measured in weeks
rather than a verifier measured in years.

## 7. Inference

Annotation burden, not syntax, is the adoption blocker (section 8). The lever is
deriving contracts from bodies and delivering them as fix-its, so a human edits
a proposal instead of authoring from a blank line. The first draft was too
optimistic about this; what survives:

**Leaf-function `writes`, plus Houdini-pruned `pre` candidates.** For a leaf
function, the write set is a dataflow fact: collect stores, resolve each to a
root, drop locals. For `pre`, propose a large candidate set (each pointer
parameter non-null, each length parameter positive), run the phase 3 checker,
delete whatever fails, keep the maximal consistent subset. This is Houdini, it
needs the fast checker phase 3 provides, and it gives phase 3 something to eat
on code nobody has annotated.

**What does not survive contact with real C:**

- "`writes` is computed, not guessed" is only true for leaves. Real stores go
  through loaded pointers (`dctx->litPtr[i] = x`), whose root is "anything
  reachable from dctx". Function pointers (openzl codec dispatch, sqlite's
  `sqlite3_io_methods` VFS, `ZSTD_customMem`) resolve to `writes(anything)` and
  poison callers transitively. Frama-C's Inout plugin and SPARK's synthesized
  `Global` both report the same finding: the sound answer is often too coarse to
  be useful.
- `pre` from unconditional dereference is what the nullsafe fork's
  `-Rnullsafe-evidence` already emits. The increment is roughly zero.
- Corpus-driven (Daikon-style) inference: not novel, and the first draft's claim
  that "nobody is doing this" was wrong. Daikon has had a C front end (Kvasir,
  Valgrind-based) since about 2005, and Nimmer and Ernst published Daikon plus
  static pruning in 2002. The failure mode is also specific: fuzz corpora are
  biased toward minimized crashers, so it will confidently propose
  `dstCap == 131072`. Kvasir also needs Valgrind, which does not work on arm64
  macOS.
- Fix-its are per-diagnostic per-TU. A header prototype included from N
  translation units yields N proposals for one line, and they conflict when TUs
  differ in macro configuration (`ZSTD_MULTITHREAD`, `DYNAMIC_BMI2`,
  `SQLITE_OMIT_*`); `clang-apply-replacements` drops conflicts. **Inferred
  `writes` is build-configuration dependent.**
- Nobody meaningfully reviews a ten-thousand-line generated patch. Proposing at
  that volume is how bad contracts get ratified, not how good ones do.

**The tautology problem is worse than the first draft said.** It correctly noted
that a contract inferred from a body and checked against that body proves
nothing. What it missed: inferred contracts are the *strongest observed*
behavior, not the *weakest needed* one. Ratifying an inferred `pre` on a public
function narrows the library's contract, and under trap semantics crashes
existing callers that were previously fine. That is a compatibility break, not a
description. Inference output must be scoped to internal functions, or gated
behind `observe` rather than `check`, until a human has deliberately widened it.

Value remains where it always was: at call sites, where phase 3 consumes a
contract that cost nothing, and at the next edit, when the body changes and the
contract does not.

## 8. Prior art

The first draft cited ACSL and hand-waved the rest. The omissions materially
changed the plan.

- **CBMC function contracts** (2021 onward): `__CPROVER_requires`, `ensures`,
  `assigns`, `frees`, `loop_invariant`, `decreases`, `old`, `return_value`,
  `forall`, plus a goto-synthesizer that infers loop invariants and assigns
  clauses using CBMC as the oracle. That is the cut phase 4 and much of phase
  3.5, already existing for C, deployed on aws-c-common, FreeRTOS, s2n. This is
  why section 6 exports to CBMC rather than rebuilding it.
- **Microsoft SAL**: `_In_reads_(n)`, `_Out_writes_(cap)`, `_Post_satisfies_`,
  checked by PREfast across all of Windows for two decades. The largest C
  contract deployment ever attempted, in macro-attribute syntax. Inside
  Microsoft the syntax did not block adoption; CI enforcement drove it. Outside,
  the free macros went unused. **This is the strongest single piece of evidence
  that ergonomics is not the lever.**
- **GCC `access` attribute** since GCC 10: `valid` plus `writes` for C, driving
  `-Wstringop-overflow`. Absent from clang. See section 10.
- **Frama-C / ACSL**: dates from 2008, not "thirty years" as the first draft
  said. Already delivers all three tiers from one source (E-ACSL runtime, EVA
  static, WP deductive), and its Inout plugin has computed per-function outputs,
  which is inferred `writes`, for as long as it has existed.
- **SPARK/Ada**: `Global` and `Depends` are frame conditions, and GNATprove
  synthesizes them when omitted. AdaCore's experience with access types matches
  the coarseness finding in section 7.
- **VCC** (used on Hyper-V) and **VeriFast**: both abandoned around a 2 to 3x
  annotation-to-code ratio.
- **Infer bi-abduction**: the first draft cited this as proof that inferring
  preconditions scales. It inverts Meta's own conclusion. Bi-abduction was
  deprecated in favor of Pulse because over-approximate inferred preconditions
  produced unacceptable false-positive rates.
- **C++26 P2900**: shipped. Copy its semantics (evaluation modes, the violation
  handler, `post(r: ...)` result naming, the by-value-parameter rule) rather
  than relitigating them. Bloomberg's clang implementation was under review for
  over eighteen months and is not merged at this base, which is the right
  calibration for phase 1's cost.
- **GCC** has shipped `-fcontracts` since 13.
- **`-Wthread-safety`** got wide adoption at Google in attribute syntax, which
  is one more data point against the ergonomics thesis.
- **WG14**: contracts-for-C papers derived from P2900 have been presented with
  no follow-through. Upstreamability (question 4) is near zero until WG14 moves.

## 9. Evaluation

The first draft proposed counting violations found by replaying a fuzz corpus
under `check`. That metric reads zero and proves nothing: zstd has been on
OSS-Fuzz since 2016 under ASan, UBSan, and MSan, so anything expressible as a
runtime contract that the corpus reaches is already a sanitizer finding. It is
also gameable, since weak contracts pass trivially.

**Replace with mutation-seeded detection.** Inject N known bugs into the target
(off-by-one bound, swapped arguments at a call site, a dropped null check, a
missing capacity check), then report detection rate per tier against a baseline
of ASan plus the existing corpus plus stock CSA. Metrics:

1. Seeded-bug detection rate, contracts versus baseline, per bug class.
2. Hand-triaged true-positive rate on unmodified code at a fixed report budget,
   in the style of the existing sqlite differential gate.
3. Annotation cost: lines of contract per line of code, and wall-clock to
   annotate one real API.
4. Call-site precision: the phase 3 measurement on 20 hand-annotated zstd
   functions, decided before phase 3 is built.

**Minimum result that justifies continuing**: contracts beat the ASan plus
corpus plus CSA baseline on at least one bug class. Public-API call-site misuse
is the likely one, because sanitizers only see the paths a corpus reaches and
CSA does not know the API's rules.

**Result that should stop the project**: contracts catch only what sanitizers
already catch.

## 10. Open problems, not yet designed

The first draft did not consider any of these.

- **Separate compilation and mixed semantics.** `pre` is checked in the callee's
  TU. All of zstd's internal headers are `static inline` (`MEM_STATIC`), so the
  same function is instantiated per TU with whatever semantic that TU used, and
  `check` in one TU with `ignore` in another is live. P2900 spends much of its
  page count here.
- **ABI and consumer impact.** A `libzstd.so` built with `check` traps its
  consumers' pre-existing benign misuse without their opt-in. This is why an
  `observe` mode is needed (section 6).
- **UB inside predicates.** `pre(p->len > 0)` with null `p` is UB during
  checking. Source-order evaluation helps only if authors order clauses
  correctly.
- **Variadic functions.** `sqlite3_mprintf`, `sqlite3_log`, `sqlite3_config(int,
  ...)`. Nothing can be said about `...`.
- **`restrict` and `const`.** `disjoint` duplicates `restrict`; decide whether
  it feeds the optimizer. A `const T*` pointee can still change during the call,
  so any `post` over pointee data is unsound without `writes`.
- **Function pointers.** Both named evaluation targets dispatch through them
  (openzl codec dispatch, sqlite's VFS and `xFunc`), which makes them the worst
  possible targets for a design that defers function-pointer contracts. Section
  4 defers them anyway; this is the cost.
- **Error paths.** Covered in section 4 item 5. Without behaviors, `writes` lies
  about partial writes on error paths.
- **`setjmp`/`longjmp` and signals.** `post` at return is bypassed by a longjmp,
  which sqlite's fault-injection harness uses. `writes` is violated by signal
  handlers.
- **Drift.** Prototype and definition live in different files. Only the static
  tiers or a corpus under `check` catch a contract that stopped matching its
  code. Inference makes drift worse by generating fossils.
- **Tooling.** clang-format, clangd, PCH and module serialization, ASTImporter
  for cross-TU. The attribute spelling makes all of these nearly free, which is
  a further argument for section 5.
- **Who validates the specs.** A too-strong `pre` under trap semantics is a
  production crash caused by the safety feature.

## 11. Effort

Engineer-months for one clang-experienced engineer. Estimates, not measurements.

| Phase | Estimate | Note |
|---|---|---|
| 1, attributes | 2-3 | Suffix syntax would have been 5-8; Bloomberg's P2900 front end took a team 18+ months and is unmerged |
| 2, check/observe/ignore | 2-3, plus 2-3 annotating three public APIs | |
| 3, CSA host | 4-6 to a usable false-positive rate on sqlite | The first draft's "bugs for nearly free" was its most underestimated line |
| 3, Sema dataflow host | 8-12 | Calibrate against the nullsafe fork's own timeline |
| 3.5, leaf `writes` plus Houdini `pre` | 2-3 | Worth it |
| 4, export to CBMC | 0.5-1 | Replaces a 12-24 month verifier |

Roughly 12-18 engineer-months for the surviving plan, against 35-60 for the
first draft.

## 12. Unresolved questions

1. **Behaviors, or no `post` on error paths?** Section 4 item 5 says every target
   function needs "on success X, on error Y". Adding ACSL-style behaviors is
   scope; omitting them makes `post` and `writes` wrong on error paths. No third
   option identified.
2. **Does phase 3 survive its own measurement?** If the 20-function experiment
   says frame conditions do not improve call-site precision on codec code, phase
   3 shrinks to `pre` checking and phase 3.5's `writes` inference loses most of
   its purpose.
3. **`observe` semantics.** Diagnose how, given a violation is detected at
   runtime? A handler call that logs and continues, matching P2900's observe.
4. **Upstreamable, or permanently a fork?** Near zero until WG14 moves (section
   8). A permanent fork means contracts exist only where this compiler runs, so
   nobody annotates, so phase 3 has nothing to consume. This remains the
   project-killing risk and it is not technical.
5. **Phase 3 host: Sema dataflow or CSA?** `LifetimeSafety` proves the
   dataflow-as-warning route ships. CSA is more precise and path-sensitive.
   Starting with the wrong one costs a rewrite. The effort table says CSA is
   half the cost.
6. **Is `access` the better first project?** Implementing GCC's `access`
   attribute in clang is a few weeks, immediately useful, upstreamable, and
   overlaps `valid` plus `writes` substantially. It may be the honest first
   deliverable regardless of what happens to the rest of this document.
