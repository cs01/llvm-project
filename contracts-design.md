# Readable contracts for C in clang

Status: design draft, nothing implemented.
Date: 2026-09-04.
Base: branch `contracts-c-dev`, forked from upstream `main` 2fd31faf6ec5 (2026-09-04).

## 1. Goal

Add a first-class contract syntax to C in clang: preconditions, postconditions,
loop invariants, and frame conditions that are part of the grammar rather than
macros, comments, or attribute soup. Readable enough that a compression library
maintainer will actually write them, and useful in three separate ways from the
same source text:

- caught at runtime (a trap at the earliest wrong state),
- checked statically at call sites (path-sensitive analysis),
- proved (SMT, opt-in, per function).

C only. No C++ interop story in v1. Not required to work outside clang.

## 2. What contracts can and cannot guarantee

Three distinct tiers get conflated in most discussions. They are not the same
tool and they do not have the same cost.

| Tier | Guarantee | Annotation cost | Precedent |
|---|---|---|---|
| Runtime-checked | None statically. Converts silent corruption into a deterministic trap at the earliest wrong state. | Low | C++26 P2900, `assert` |
| Path-sensitive bug finding | Unsound and incomplete. Finds real bugs, proves nothing. | Low | clang static analyzer |
| Deductive verification | Proves "precondition implies postcondition, no UB" for that function. | High: invariants, variants, frame conditions, memory model | Frama-C/WP, VeriFast, VCC |

Only the third tier "guarantees", and it guarantees the code matches the spec.
It does not guarantee the spec is right. A wrong `post` is proved happily.

The practical position: tiers 1 and 2 pay for themselves at low annotation cost
and should ship first. Tier 3 is opt-in on a handful of leaf functions where the
payoff justifies writing loop invariants by hand.

### Rust comparison

Porting to Rust buys spatial and temporal memory safety. It does not buy
functional correctness. A decoder that mis-parses a frame header and returns
wrong-but-in-bounds bytes is a safe Rust program and a broken decompressor.
Contracts attack the axis Rust leaves alone. This is the reason to do the work
on C rather than treat C as a lost cause.

### CSA is not an SMT solver

Correct. The clang static analyzer is symbolic execution over an exploded graph
with a cheap `RangeConstraintManager`. It can already dispatch to Z3 in two
ways, both present in this tree:

- `-analyzer-constraints=z3`, a full SMT constraint manager
  (`clang/include/clang/StaticAnalyzer/Core/PathSensitive/SMTConstraintManager.h`,
  `SMTConv.h`, over `llvm/include/llvm/Support/SMTAPI.h`),
- Z3 refutation of finished reports (`crosscheck-with-z3` in
  `AnalyzerOptions.def`).

So the solver plumbing is in-tree already. Tier 3 reuses it rather than growing
a new one.

## 3. Recon: what already exists in this tree

Checked 2026-09-04 against the fork base.

- **No C or C++ contracts.** `grep -rn "ContractStmt\|contract_assert" clang/include clang/lib` is empty. C++26 P2900 is not implemented here. Nothing to retarget, but also nothing to fight.
- **`__counted_by` / `__sized_by` exist.** `clang/include/clang/Basic/Attr.td:2660` (`CountedBy`, `CountedByOrNull`), with `clang/lib/Sema/SemaBoundsSafety.cpp`. Apple's `-fbounds-safety` is precedent that adding real C language surface to clang for safety is an accepted path, and gives a working model for "a parameter's bound is another parameter".
- **`[[assume]]` / `__attribute__((assume))`** at `Attr.td:1949`, lowering to `llvm.assume`. This is the tier-1-to-optimizer bridge and it already works.
- **`diagnose_if`** at `Attr.td:3754`. Closest existing thing to a compile-time precondition, and a good example of the ergonomics problem this design is trying to fix.
- **SMT layer** present as listed above.
- **`clang/lib/Analysis/LifetimeSafety/`** (~4k lines, new since May 2026). A
  CFG dataflow analysis that verifies function bodies against their lifetime
  annotations (`[[clang::lifetimebound]]`, `[[clang::noescape]]`), wired into
  ordinary warnings via `AnalysisBasedWarnings.cpp`, not into the opt-in static
  analyzer. Its own docs call the annotations "contracts". This is the closest
  structural precedent in the tree and a candidate host for phase 3: a
  per-TU dataflow pass that ships as a warning reaches far more users than a
  CSA checker anyone has to opt into.
- Note: driver options moved to `clang/include/clang/Options/Options.td`.

## 4. What the language actually needs

Minimum viable spec language for real C. Ordered by how much each one is
missed when absent.

1. **`pre` / `post`.** Table stakes.
2. **Result naming in `post`.** `post(r: r <= cap)`.
3. **`old(e)` in `post`.** Requires an entry snapshot. Scalars only in v1; snapshotting memory needs the tier-3 memory model.
4. **Frame conditions (`writes`).** The one people forget and the one that decides whether tiers 2 and 3 work at all. Without "this call modifies only `dst[0..cap)`", every call invalidates every fact and modular reasoning collapses. Today's CSA havocs aggressively at calls for exactly this reason.
5. **`invariant` and `variant` on loops.** Invariant cuts the loop for verification; variant proves termination.
6. **Memory predicates.** `valid(p, n)`, `disjoint(a, n, b, m)`. `valid` overlaps `__counted_by` and should reuse its Sema machinery where possible.
7. **Quantifiers over ranges.** `forall k in [0, n): a[k] == b[k]`. Unavoidable for anything buffer-shaped, which for a compression library is everything.
8. **Purity, and a `spec` function concept.** Only side-effect-free functions may appear in predicates, so that `post(is_sorted(a, n))` is meaningful. Needs a way to declare a function usable in specs and checked for purity.
9. **Ghost variables and ghost parameters.** For proofs that need history the program does not keep. Tier 3 only.

Deliberately out of scope for v1: type invariants on `struct`, contracts on
function pointers, separation-logic ownership. Each is a large scope increase
and none is needed to get value out of tiers 1 and 2.

### Prior art

ACSL (Frama-C's spec language for C) is the mature answer to "what does C
need": `requires`, `ensures`, `assigns`, `loop invariant`, `loop variant`,
`\old`, `\result`, `\valid`, `\separated`, ghost code, logic functions,
axiomatics. Steal the semantics. Reject the syntax: ACSL lives inside
`/*@ ... */` comments, which is precisely the property this project rejects.

## 5. Syntax

Contextual keywords behind `-fc-contracts`, in the declarator suffix. Not
attributes: the whole complaint about `__attribute__((returns_nonnull))` is
that it reads like an annotation bolted to the side rather than part of the
declaration.

```c
size_t ZL_decompress(void *dst, size_t dstCap,
                     const void *src, size_t srcSize)
  pre   (valid(dst, dstCap))
  pre   (valid(src, srcSize))
  pre   (disjoint(dst, dstCap, src, srcSize))
  post  (r: r <= dstCap || ZL_isError(r))
  writes(dst[0..dstCap]);
```

```c
for (size_t i = 0; i < n; i++)
  invariant (i <= n)
  invariant (forall k in [0, i): out[k] == lit[k])
  variant   (n - i)
{
  out[i] = lit[i];
}
```

Rules:

- Contracts are declared on the prototype and inherited by the definition. A
  definition may restate them; Sema checks they match. Header-visible means
  call-site checkable, which is the entire point.
- Predicates are ordinary C expressions of type `int`/`_Bool`, plus the spec
  builtins (`old`, `valid`, `disjoint`, `forall`, `exists`).
- Predicates must be pure. Calls inside a predicate must be to functions marked
  usable in specs.
- Multiple `pre`/`post` clauses conjoin, in source order. Order matters for
  runtime checking (check `p != NULL` before `p->len > 0`).

Open syntax risk: declarator-suffix parsing collides with the C attribute
grammar and with K&R declarations in corners. Contextual keywords keep existing
code compiling (`int pre = 3;` stays legal) at the cost of parser lookahead.

## 6. Implementation plan

### Phase 0: recon
Done, section 3 above. Conclusion: greenfield front end, reuse the SMT and
bounds-safety layers.

### Phase 1: front end
- Lexer: contextual keywords `pre`, `post`, `writes`, `invariant`, `variant`,
  `forall`, `exists`, `old`, `valid`, `disjoint`, active only under
  `-fc-contracts`.
- Parser: declarator-suffix clauses in `ParseDecl.cpp`; loop clauses between
  the loop header and body in `ParseStmt.cpp`.
- AST: `ContractSpecifier` attached to `FunctionDecl`, `LoopContract` attached
  to `ForStmt`/`WhileStmt`/`DoStmt`. Serialization, `ASTDumper`, AST matchers.
- Sema: predicate type checking, purity checking, `old`/result binding,
  prototype-vs-definition consistency, diagnosis of `old` outside `post`.

Gate: lit tests for parse, sema, and `-ast-dump`. No codegen.

### Phase 2: lowering, three modes
`-fcontract-semantic={ignore,check,assume}`.

- `check`: emit a branch plus a handler call per clause. Reuse the ubsan
  handler plumbing for the runtime interface and source-location encoding.
  `pre` at callee entry, `post` at every return, with an entry snapshot for
  `old` on scalars.
- `assume`: emit `llvm.assume`. This is the performance story: contracts feed
  the optimizer facts it cannot derive across a call boundary, letting it
  delete redundant bounds and null checks. Contracts that make the decoder
  faster, not just safer.
- `ignore`: parse and Sema-check, emit nothing.

Gate: openzl, zstd, and sqlite build clean with contracts on the public
headers; full upstream test suites green under `check` with zero violations, or
with violations that turn out to be real bugs, which is a better outcome.

### Phase 3: whole-body checking against contracts
Two possible hosts, see question 8:

- **CFG dataflow in Sema**, alongside `LifetimeSafety`, shipping as a warning.
  Cheap, on by default, reaches everyone; less precise.
- **CSA checker**, path-sensitive with a real constraint manager. More precise,
  opt-in only.

Either way the checker consumes contracts the same way:

- at a call site, **check** the callee's `pre` against the current symbolic
  state and report a violation,
- after the call, **assume** the callee's `post`,
- use `writes` to invalidate only the named region instead of the current
  aggressive havoc.

The `writes` half is the interesting part: it should make CSA strictly more
precise on annotated code, which is a measurable improvement independent of
whether anyone writes a single `post`.

Gate: run over openzl and sqlite, hand-triage findings, report true-positive
rate.

### Phase 4: deductive verification, opt-in per function
`-fcontract-verify` on marked functions.

- Build verification conditions from the CFG: weakest precondition computed
  backward over each block, loops cut by their `invariant`, termination from
  `variant`.
- Encode to Z3 through the existing `SMTAPI` layer.
- Report unproved goals as diagnostics pointing at the specific clause.
- Memory model: typed and block-based, in the style of CBMC. Scalars and
  single-dimension buffers. No pointer arithmetic across objects.

Explicitly not a Frama-C replacement. The target is "prove these thirty leaf
functions", not "prove the library".

Phases 1 and 2 are independently useful and testable against real code. Phase 3
is where bugs get found for nearly free. Phase 4 is research-shaped and must
not block the rest.

## 7. Evaluation targets

A number that moves per commit, or it is churn.

### openzl (github.com/facebook/openzl)
Cloned and surveyed. 208k lines of C across 321 files, plus 159k lines of C++.
The core is C: `src/openzl/{compress,decompress,codecs,shared,common}`, with
public headers under `include/openzl/*.h` in `extern "C"`. Codec directories
(`codecs/lz`, `codecs/rolz`, `codecs/tokenize`, `codecs/bitSplit`,
`codecs/pivco_huffman`) are exactly the buffer-walking, bit-reading code that
contracts are for. `src/openzl/shared/{bits.h,mem.h,overflow.h,varint.h}` is a
small, self-contained, heavily-used layer and is the right first target: leaf
functions, scalar arithmetic, no allocation, provable in tier 3.

The library already has an explicit error-report type (`ZL_Report`) and
macro-based error propagation, so contracts complement rather than duplicate
the existing discipline.

Caveat: the C++ half is not covered by a C-only design, so "openzl is fully
annotated" is not reachable in v1. Public C API plus the C codecs is.

### zstd
Harder and higher payoff. Mature fuzz corpus to replay under `check` mode.

### sqlite
Larger surface, and there is prior verification work to compare against.

Proposed metrics:

1. Violations found replaying an existing fuzz corpus under `-fcontract-semantic=check`.
2. Decode throughput under `assume` vs baseline. Target: neutral to positive.
3. CSA findings on annotated code before and after phase 3, hand-triaged for
   false-positive rate.
4. Functions discharged by phase 4, and solver time per function.

Metric 3 mirrors the existing sqlite differential gate methodology.

## 8. Unresolved questions

1. **Syntax placement.** Declarator suffix (as drafted) versus a prefix block
   versus attributes with real predicate parsing. Suffix reads best, collides
   worst with the existing C grammar.
2. **Frame conditions in v1 or v2?** They are what make phases 3 and 4 work,
   and they are the heaviest annotation burden. Shipping without them makes
   phase 2 easy and phase 3 weak.
3. **`struct` type invariants and contracts on function pointers.** Both
   deferred above. Confirm that is acceptable, since openzl's codec dispatch is
   function-pointer heavy and phase 3 will lose precision at those calls.
4. **First target library**: openzl `shared/` leaf functions, or zstd's decoder?
5. **Upstreamable, or permanently a fork?** Changes the cost of the syntax
   bikeshed by an order of magnitude.
6. **Runtime violation behavior** under `check`: trap, `__builtin_unreachable`,
   or a user-installable handler. P2900 chose an installable handler; a
   compression library probably wants a trap.
7. **Interaction with `__counted_by`.** Is `valid(p, n)` sugar over the existing
   bounds-safety attribute, or an independent predicate? Sugar is less code and
   fewer semantics to define, but ties the design to `-fbounds-safety`'s model.
8. **Phase 3 host: Sema CFG dataflow or the static analyzer?** `LifetimeSafety`
   proves the dataflow-as-warning route works and gets shipped. CSA is more
   precise and already has Z3. Doing both eventually is fine; starting with the
   wrong one costs a rewrite.
