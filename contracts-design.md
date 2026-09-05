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

## 7. Inference: the answer to annotation burden

Syntax is not why deductive verification has stayed niche. Frama-C is good,
free, and thirty years old, and adoption is still narrow. The blocker is that
someone has to sit down and write the annotations, and keep writing them
through every refactor. No amount of grammar fixes that.

The lever that might: **derive contracts from the body and deliver them as
fix-its**, so the human reviews and edits a proposal instead of authoring from
a blank line. This changes the adoption question from "will anyone write ten
thousand annotations for zstd" to "will anyone review a ten-thousand-line
generated patch". The second question has a much better answer.

### What is actually inferrable, in descending order of payoff

1. **`writes` (frame conditions).** The best target by a wide margin. A
   function's write set is a plain dataflow fact: walk the CFG, collect every
   store, resolve each to a root (parameter, global, local), drop the locals.
   What remains is the frame. This is computed, not guessed. It is also the
   annotation with the highest value (sections 4 and 6 depend on it) and the
   highest burden, so inference lands exactly where it is needed. When a store
   goes through a pointer of unknown provenance the answer degrades to
   `writes(anything)`, which is honest and still useful: it names the functions
   that need a human.

2. **`pre` from unconditional use.** A parameter dereferenced on every path
   before any assignment to it implies `pre(p != NULL)`, or the function has UB
   on some input. Same shape for indexing: `a[i]` with `i` bounded by parameter
   `n` implies `pre(valid(a, n))`. The nullability fork already computes this
   class of fact and already ships it as `-Rnullsafe-evidence` remarks for
   migration tooling, so this is reuse rather than new work.

3. **`post` from return-value dataflow.** Weaker, but real for the
   size-returning functions that make up most of a compression API: if the
   returned variable is only ever assigned values bounded by a parameter,
   propose `post(r: r <= cap)`.

4. **Loop invariants.** The hard one. Abstract interpretation over an interval
   or octagon domain, with widening, gets the range-shaped invariants
   (`i <= n`) for free. Those are the boring ones, and they are also most of
   what a human would otherwise type. It will not get the relational ones
   (`forall k in [0, i): out[k] == lit[k]`). Realistic split: the machine
   proposes the ranges, the human writes the one invariant that carries the
   actual meaning.

### Prior art worth copying

- **Bi-abduction (Infer).** Infers preconditions for heap-manipulating C at
  millions of lines. The scaling story is proven and it is Meta's own.
- **Houdini.** Propose a large candidate annotation set, run the checker,
  delete whatever fails, repeat to the maximal consistent subset. Needs a fast
  checker in a loop, which phase 3 provides.
- **Daikon.** Infers likely invariants dynamically from execution traces.
  Underrated here: zstd, openzl and sqlite all ship industrial fuzz corpora
  and test suites. Running the corpus and observing that `dstCapacity` always
  exceeds the returned size is cheap evidence for a proposed contract, and it
  proposes facts that no static domain would find. Corpus-driven inference is
  a good fit for exactly these libraries and is not what anyone else is doing.

### Delivery

Fix-its, not a report. Clang already has `FixItHint`, `-Xclang -fixit`, and
`clang-apply-replacements`. Workflow: build with `-fcontract-infer`, get a
patch that annotates the headers, review the diff, commit the parts that are
right. The fork's `-Rnullsafe-evidence` to migration-tooling pipeline is the
same shape and can be the template.

### The trap: inferred contracts are tautologies

A contract derived from a body and then checked against that same body proves
nothing. It passes by construction. Any tool that infers and then reports "0
violations, verified" is lying, and someone will read it that way.

The value of an inferred contract is entirely elsewhere, in two places:

- **At call sites.** Phase 3 checks callers against the callee's contract, and
  the callee's contract was free. This is where inferred `writes` pays off
  immediately, with no human in the loop at all.
- **At the next edit.** The inferred contract is a snapshot of what the
  function did on the day it was inferred. It starts earning the moment the
  body changes and the contract does not.

So inference output is a *proposal a human ratifies*, and the tooling must
present it that way. Once ratified it is a specification; until then it is a
description.

### Where this sits in the plan

Inference needs the checker to exist first: you can only infer what you can
express and verify. So it is phase 3.5, after the checker and before anyone is
asked to annotate a real library by hand. That ordering also gives the
bootstrap sequence for tier 3: infer `writes` mechanically across the whole
library, which is what makes modular reasoning possible at all, then have
humans write only `post`, which is the part that carries actual intent.

## 8. Evaluation targets

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

## 9. Unresolved questions

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
9. **How much inference before the first human annotation?** Inferring `writes`
   across a whole library is mechanical and unlocks phase 3. Inferring `pre` is
   nearly as cheap. Doing both before asking anyone to type a contract may be
   the difference between a tool people use and a tool people admire. It also
   delays the first end-to-end demo considerably.
10. **Static or corpus-driven inference first?** Static is sound-ish and needs
    no test suite. Corpus-driven (Daikon-style, over the existing fuzz corpora)
    proposes facts static analysis cannot reach, but every proposal is a guess
    that needs ratifying. They are complementary; the question is ordering.
