# Readable contracts for C in clang

Status: phase 1 started. `-fc-contracts` parses and type-checks `pre` clauses
(commit `7122f3da6450`); everything else below is still design.
Priority: **compile-time checking first.** Section 6 is ordered accordingly, and
this reverses the original phase order; see "Ordering" there for what that costs.
Scope: **the language comes first and compatibility with compilers that will not
implement it comes last.** That is not a slogan; it decided three things below
(the attribute spelling is cut, `writes` gets a `when` guard rather than a
portable-looking encoding, and the shim is in appendix A).
Independence: contracts do not depend on, and must not be entangled with, the
flow-nullability work on `nullsafe-clang-dev`. Phase 3 is a standalone pass.
Revised: 2026-09-04, after an adversarial review. Findings that survived
checking are marked where they changed the design, so the reasoning is not
silently rewritten. The review also argued the readable syntax was unachievable
and that this should be an attribute-only feature; that argument did not survive
checking (section 5) and was rejected.
Base: branch `contracts-c-dev`, forked from upstream `main` 2fd31faf6ec5
(2026-09-04). Every in-tree citation below was checked against that commit.

## 1. Goal

Give C first-class contracts in clang: preconditions, postconditions, and frame
conditions that the compiler parses and checks, rather than macros that expand
to nothing or comments no tool reads.

Three requirements, none of which is traded against the others:

- **Readable.** Contracts are grammar, in the declaration, in a form a
  maintainer will write voluntarily. Not `__attribute__` soup, not SAL macros,
  not `/*@ ... */`.
- **Enforceable.** Every clause is checked by something: a runtime trap, a
  call-site diagnostic, or a proof. A clause nothing checks is a comment.
- **Real C.** It works on zstd, openzl, and sqlite as they are actually written,
  not on a sanitized subset.

Useful in two ways from one source text:

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
  GCC has had `access(write_only, 1, 2)` since GCC 10: positional, per-parameter,
  no predicates, a crude `valid` plus `writes` that drives `-Wstringop-overflow`.
  Relevant as evidence that this semantic content is deployable and useful in a
  production compiler, and as an interop target for lowering `writes`. Not a
  substitute for the design here, and not a smaller thing to build instead of
  it.
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
5. **Error paths: `when` on a frame condition.** Every function in the target
   libraries fills `dst[0..r)` on success and leaves it untouched on error, and
   a specification that cannot say so lies on half its executions.

   The problem is narrower than it first looks, and the first draft of this
   section got it wrong by calling for ACSL-style named behaviors. A `post` is a
   predicate, and a predicate already expresses case analysis with the operators
   C has:

   ```c
   post (r: is_error(r) || r <= old(dstCap))
   ```

   That parses and type-checks today. No new syntax buys anything there.

   A `writes` is not a predicate. It is a set of locations, and a set cannot be
   disjoined, so `writes (dst, r)` on success and `writes ()` on error genuinely
   cannot be written as one clause. That, and only that, is the gap. It calls for
   the smallest thing that closes it: a guard on the clause.

   ```c
   unsigned long decompress(void *dst, unsigned long dstCap,
                            const void *src, unsigned long srcSize)
     pre    (dst != 0)
     pre    (dstCap > 0)
     post   (r: is_error(r) || r <= old(dstCap))
     writes (dst, r) when (r: !is_error(r));
   ```

   An unguarded `writes` is unconditional, which is the common case and stays
   the short spelling. `when` takes the same result binding a `post` does, so
   there is one rule to learn rather than a new block construct with its own
   scoping. Named behaviors are rejected: they introduce a second place where
   pre- and postconditions live, and every example in the target libraries
   discriminates on the *result*, which ACSL's `assumes` (a pre-state condition)
   cannot see anyway.
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

Contracts are real grammar. They are lexed, parsed into expressions with the
function's parameters in scope, type-checked, and diagnosed like any other
construct. They are not macros that expand to nothing and not comments.

```c
size_t ZSTD_decompress(void *dst, size_t dstCap,
                       const void *src, size_t srcSize)
  pre    (valid(dst, dstCap))
  pre    (valid(src, srcSize))
  pre    (disjoint(dst, dstCap, src, srcSize))
  writes (dst, dstCap)
  post   (r: r <= old(dstCap) || ZSTD_isError(r));
```

```c
for (size_t i = 0; i < n; i++)
  invariant (i <= n)
  variant   (n - i)
{
  out[i] = lit[i];
}
```

### Where it parses

In the declarator suffix, inside `ParseFunctionDeclarator`, immediately after
the closing paren. Clang already parses three things in exactly that position
with the prototype scope still open: `tryParseExceptionSpecification`,
`MaybeParseCXX11Attributes`, and `ParseTrailingReturnType`. `noexcept(expr)` in
that slot is an arbitrary expression naming the function's parameters, which is
the same shape a contract predicate needs. Clang also already has the
`ExceptionSpecTokens` delayed-parsing path for that slot, for the case where the
expression cannot be resolved until the enclosing context is complete. A
contract on a prototype needs the same deferral.

A note on an earlier objection: `ParseDecl.cpp:6992` exits `PrototypeScope` and
does put parameters out of scope, but that is the outer `ParseDirectDeclarator`
level, not this one. Parsing at the wrong level would have made the syntax
impossible; parsing at the right level is ordinary.

### Known parse hazards, and what each costs

- **`for (...) invariant(x);` is a valid call statement today** when `invariant`
  is a function. Loop clauses need lookahead, or a stricter rule that a clause
  must be followed by another clause or a compound statement.
- **K&R declarations.** If `pre` is a typedef, `int f(a) pre(a) { }` is already
  a valid K&R declaration. C23 removed K&R declarations; gate the feature on C23
  or diagnose the collision.
- **A macro named `pre`, `post`, or `valid`** in any earlier header silently
  rewrites contracts. This must be a diagnostic, not a surprise.
- **Contextual keywords in the declarator suffix** are parsed on every function
  declarator, including function pointers, abstract declarators, and `sizeof`
  operands. Sema rejects all but the one case. That is exactly what `noexcept`
  does.
- **clang-format and clangd** need to learn the grammar.

None of these is an obstacle to the design. Together they are roughly the
difference between a 2-3 and a 5-8 engineer-month front end (section 11). That
is the price of the readable form and it is worth paying.

Range syntax is a call, `writes(dst, dstCap)` and `valid(p, n)`, not
`dst[0..dstCap]`. `0..dstCap` lexes as a single pp-number and produces
`error: invalid suffix '.2' on floating constant`. Verified. This is a spelling
choice inside the design, not a constraint on it.

### Other compilers

Deliberately not designed for here. Making the syntax palatable to compilers
that will never implement it is the fastest way to end up with SAL, where the
macro *is* the feature. The design question is what a contract should say; the
answer to "what does GCC do with this" is a one-line macro and it is written
down in appendix A, at the back, where it belongs.

### Rules

- Contracts are declared on the prototype and inherited by the definition. A
  definition may restate them; Sema checks they match. Header-visible means
  call-site checkable, which is the point.
- Clauses conjoin in source order. Order matters under runtime checking, so
  `p != NULL` must precede `p->len > 0`.
- Predicates are pure. Calls inside a predicate must be to functions marked
  usable in specs.
- A prototype and its definition have distinct `ParmVarDecl`s, so the predicate
  expression must be rebound at the definition. `sqlite.h.in` has 186 prototype
  lines with unnamed parameters, so positional parameter references are needed
  as well.


## 6. Implementation plan

### Ordering

Phases are numbered by dependency, not by the order they get built. The build
order is **1, 3, 2**: the front end, then static call-site checking, then
runtime checking. This is a deliberate reversal of the original plan and the
reason is that only the static tier is worth the annotation burden on its own.
A `pre` that merely traps at runtime competes with `assert`, which is already
written, already free, and already understood. A `pre` that a compiler checks at
every call site is something no C project has.

What the reversal costs, stated plainly:

- Phase 2 was the cheap confidence-builder that proved the annotations were
  writable before anything expensive got built. Doing phase 3 first means the
  first real feedback on whether the syntax survives contact with `zstd.h`
  arrives later and costs more.
- Phase 3 is unsound and incomplete (section 2). It ships warnings, not
  guarantees. Leading with it means the first thing users see is a false
  positive rate, which is a harder first impression than a trap that fires
  exactly when the program was already wrong.

Both are accepted. The mitigation for the first is that phase 1's lit tests plus
the 20-function zstd annotation experiment (phase 3, below) exercise the syntax
on real headers without needing codegen. The mitigation for the second is that
the host analysis already has a measured false-positive profile; see phase 3.

### Phase 0: recon
Done, section 3.

### Phase 1: front end
Lexer: contextual keywords active only under `-fc-contracts`. Parser: clauses in
the declarator suffix inside `ParseFunctionDeclarator`, alongside the
exception-spec, reusing the delayed-token path for prototypes; loop clauses
between the loop header and body. AST: `ContractSpecifier` on `FunctionDecl`,
`LoopContract` on the loop statements. The attribute spelling parses to the same
nodes. Sema: predicate type checking, purity, `old()` and result binding,
prototype-to-definition rebinding, positional parameter references, consistency
diagnostics, and a diagnostic for a macro colliding with a clause keyword.
Serialization, `ASTDumper`, AST matchers, and clang-format support.

Gate: lit tests for parse, sema, `-ast-dump`, and clang-format. No codegen.

**Done so far** (commit `7122f3da6450`, 523 lines, full clang suite clean at
54699 tests):

- `-fc-contracts` / `-fno-c-contracts` and `__has_feature(c_contracts)`.
- `pre (expr)` parsed in the declarator suffix at the point section 5 predicted.
  The prediction held: `ParseFunctionDeclarator` asserts
  `isFunctionPrototypeScope()` for its entire body (`ParseDecl.cpp:7283`) and the
  scope is exited by the caller at `:6992`, so parameters are in scope for the
  predicate with no new scope machinery.
- `ContractClause` / `ContractSpecifier` in `AST/ContractSpecifier.h`, hanging
  off `FunctionDecl` and deliberately not off its type.
- Sema: predicate goes through `CheckBooleanCondition`, so `pre (p)` means
  `pre (p != 0)` and a non-scalar predicate gets the usual diagnostic. Clauses on
  a declarator that does not declare a function are diagnosed.
- `-ast-dump`, `RecursiveASTVisitor` traversal, and PCH serialization. The PCH
  work is not optional: contracts live on the decl, so without it a `pre` in a
  header would be present in a normal build and silently gone in a PCH build.

**Not done, and why:**

- `post`: **done**, with result binding. The predicate tokens are saved and
  replayed once the `FunctionDecl` exists, because the result binding needs the
  return type and the declarator is incomplete where the clause appears: for
  `int *f(void)` the pointer chunk is added after the function chunk. Same shape
  as a delayed exception specification. Naming a parameter in a `post` is
  rejected per section 4, which is the rule `old()` will lift.
- `old()`: **done**. `ContractOldExpr`, contextual so an ordinary identifier
  named `old` is unaffected, rejected in a `pre`, and scalars only per section 2.
  With it the section 5 example type-checks as written, including the
  `post (r: r <= old(dstCap) || ...)` form the first draft got wrong.
- `writes`: parsed and hard-errored as unsupported, so the grammar is pinned now
  and cannot be silently reinterpreted later.
- Loop clauses: **done for `while`**. `invariant (expr)` is a condition and
  `variant (expr)` is a scalar measure; both must be pure. Held in an
  `ASTContext` side table rather than in the loop nodes, since contracts are
  rare and `WhileStmt` is among the most numerous nodes in any AST. `for` and
  `do` still need the same hook.

  The follow-set rule from section 12 is implemented as a token-stream
  lookahead rather than as an error: the parser scans balanced parens past the
  clause sequence and only commits to the clause grammar if a compound
  statement follows. `while (x) invariant(x);` therefore still parses as the
  call statement it has always been. Implementing it as "error if no `{`" was
  the first attempt and was wrong, because the point of the rule is to preserve
  existing meaning, not to reject the old spelling.
- Attribute spelling: question 7, still open. Everything downstream is written
  against the AST, so it stays cheap to add.
- Prototype-to-definition inheritance and rebinding: clauses are per-declaration
  today. Needed before call-site checking can consult a header.
- Macro-collision diagnostic: **done** (`PPCallbacks::MacroDefined` registered
  from `Parser::Initialize`, so `-D` and the predefines buffer are covered).
  Warning in `-Wc-contracts`, not an error: a project may have an unrelated
  `pre` macro and never write a contract.
- Purity checking: **done**. `Expr::HasSideEffects` rejects assignment,
  increment, and calls to anything not marked `const` or `pure`, so those two
  existing attributes are the "usable in specs" marker from section 4 item 7
  and no new attribute is needed.


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

**Recast: phase 2's job is now to be the oracle for phase 3.** Under a
compile-time-first plan the runtime tier stops being the headline feature and
becomes the only unfakeable measurement of the static checker's false-negative
rate. Build an annotated project with `-fcontract-semantic=check`, run its test
suite, and every trap that fires at a call site the static pass did not flag is
a measured false negative, with a reproducer attached. Nothing else produces
that number: a static checker's misses are invisible by construction, so
"phase 3 found no bugs here" and "phase 3 is blind here" look identical without
this.

That reframing also settles how much of phase 2 to build and when. The subset
needed to serve as an oracle is `pre` checking at callee entry on scalar and
pointer predicates, which is the cheap part. `post`, `old()` snapshots, and
`observe` are product features and can wait until phase 3 has a precision number
worth defending.

Gate: zstd, openzl, and sqlite build clean with annotated public headers; test
suites green under `check`; and the false-negative count above is reported, not
merely collected.

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

**Host: a standalone Sema CFG dataflow pass on this branch.**
`clang/lib/Analysis/ContractChecking.cpp`, wired through
`AnalysisBasedWarnings.cpp` the way `LifetimeSafety` is, shipping as an ordinary
warning rather than an opt-in analyzer checker.

An earlier revision proposed hosting this inside the flow-nullability fork's
`FlowNullability.cpp` to reuse its calibrated narrowing lattice. **Rejected.**
Contracts and nullability are separate features on separate branches, and
coupling them would mean neither can ship, rebase, or be reviewed without the
other. The reuse was worth less than the independence: what carries over is the
*lesson*, not the code.

That lesson is the design rule for this pass: **report only preconditions that
can be shown violated, never ones that merely cannot be proven.** Running the
nullability fork in its permissive mode produced 22,772 warnings on the sqlite
amalgamation, almost all false positives, against 131 in its trusting mode. A
checker that warns on everything unproven is not a checker anyone will run, so
"cannot tell" is silence here.

Two false-positive classes are known in advance from that calibration and should
be expected rather than discovered:

- Out-parameter writes, `T *p = 0; f(&p); p->x;`, where the analysis cannot see
  `f` write through `&p`. Handled here by refusing to track any variable whose
  address is taken.
- Correlated multi-variable invariants, where a guard on one variable implies
  something about another and the two are tracked independently. Not handled;
  it costs false negatives, not false positives, which is the right side to err
  on.

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
  the free macros went unused. Two lessons, and they point in different
  directions: enforcement matters more than spelling, and a notation no
  compiler parses (SAL macros expand to annotations only PREfast reads) does
  not travel. This design takes the first lesson and rejects the second
  condition. See section 5 on why a portability shim is not the same thing as a
  macro-defined feature.
- **GCC `access` attribute** since GCC 10: `valid` plus `writes` for C, driving
  `-Wstringop-overflow`. Absent from clang. Proof the semantics ship and pay;
  also proof that positional, predicate-free annotation is as far as anyone has
  taken C in a mainstream compiler.
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
  for cross-TU. Real grammar means clang-format and clangd must learn it, which
  is budgeted in phase 1. Getting this wrong makes every contributor hate the
  feature on day one, so it is a phase 1 gate and not a follow-up.
- **Who validates the specs.** A too-strong `pre` under trap semantics is a
  production crash caused by the safety feature.

## 11. Effort

Engineer-months for one clang-experienced engineer. Estimates, not measurements.

| Phase | Estimate | Note |
|---|---|---|
| 1, native syntax plus attribute spelling | 5-8 | Attribute-only would be 2-3; the difference is the price of the readable form. Bloomberg's P2900 front end took a team 18+ months and is unmerged, but that was full C++ with templates, constexpr, and virtual overrides |
| 2, check/observe/ignore | 2-3, plus 2-3 annotating three public APIs | |
| 3, Sema dataflow host, extending `FlowNullability.cpp` | 4-6 to a usable false-positive rate on sqlite | **Chosen.** The 8-12 below was for building a dataflow host from scratch; this fork already has a calibrated one, which is what closes the gap to the CSA number |
| 3, CSA host | 4-6 to a usable false-positive rate on sqlite | Not chosen. Would mean a second analysis and a second false-positive profile. The first draft's "bugs for nearly free" was its most underestimated line |
| 3, Sema dataflow host from scratch | 8-12 | Superseded, kept so the comparison above is auditable |
| 3.5, leaf `writes` plus Houdini `pre` | 2-3 | Worth it |
| 4, export to CBMC | 0.5-1 | Replaces a 12-24 month verifier |

Roughly 15-23 engineer-months for the surviving plan, against 35-60 for the
first draft. The readable syntax accounts for about 3-5 of the difference from
an attribute-only version, which is the single clearest cost-versus-goal
tradeoff in the document, and it is being paid deliberately.

Build order is 1, 3, 2 (section 6), so the spend is front-loaded into the front
end and the static checker. The runtime tier's 2-3 comes last, and the oracle
subset that phase 3 actually depends on is a fraction of it.

## 12. Unresolved questions

1. **Does phase 3 survive its own measurement?** If the 20-function experiment
   says frame conditions do not improve call-site precision on codec code, phase
   3 shrinks to `pre` checking and phase 3.5's `writes` inference loses most of
   its purpose.
2. **`observe` semantics.** Diagnose how, given a violation is detected at
   runtime? A handler call that logs and continues, matching P2900's observe.
3. **Upstreamable, or permanently a fork?** Near zero until WG14 moves (section
   8). A permanent fork means contracts exist only where this compiler runs, so
   nobody annotates, so phase 3 has nothing to consume. This remains the
   project-killing risk and it is not technical.
4. **`valid` and `disjoint` without bounds-safety machinery.** Section 2 says
   neither is runtime-checkable and that the static story needs allocation
   sizes the compiler does not have. They are the clauses the target libraries
   most want and the ones with no implementation path yet. Still open.

### Resolved

- **Error paths.** Section 4 item 5. A `post` expresses case analysis with `||`
  and needs nothing new; only `writes` cannot be disjoined, so it gets a `when`
  guard. Named behaviors rejected.
- **Loop clause placement.** A loop clause sequence must be followed by a
  compound statement. `for (...) invariant(x);` therefore stays exactly what it
  is today, a call statement, and `for (...) invariant(x) { ... }` is a
  contract. The cost is that a loop carrying an invariant must brace its body,
  which is a style most of the target code already follows and a rule that can
  be stated in one line. Chosen over lookahead because the follow-set rule needs
  no backtracking and produces a comprehensible diagnostic when it fails.
- **Attribute spelling.** Cut from v1; see appendix A. It exists only for
  compilers that will not implement the feature, and that concern does not get
  to shape the language.
- **Phase 3 host: Sema dataflow or CSA?** Answered in phase 3: Sema dataflow,
  extending this fork's `FlowNullability.cpp`. The effort table's CSA advantage
  assumed building a dataflow host from scratch, which is not the situation
  here. Recorded rather than deleted so the reversal is auditable.

## Appendix A: other compilers

Recorded for completeness, and deliberately not allowed to shape anything above.

`zstd.h` and `sqlite3.h` are compiled by every compiler in existence, and any
new syntax is a syntax error in all of them. The shim:

```c
#if defined(__clang__) && __has_feature(c_contracts)
#  define ZSTD_PRE(...) pre(__VA_ARGS__)
#else
#  define ZSTD_PRE(...)
#endif
```

This is the same thing every codebase already writes for `_Nullable`,
`[[nodiscard]]`, and `warn_unused_result`. It is a portability shim at one
boundary, not the definition of the feature.

This is worth distinguishing from SAL (section 8), where the macro *is* the
feature: `_Out_writes_(n)` expands to another annotation that only one
proprietary analyzer reads, and no compiler ever parses a predicate. Here the
compiler parses a real grammar, builds a real AST, and emits real diagnostics
and fix-its; the shim exists solely so that other compilers see nothing.
Collapsing these two into "macro-shaped" is a category error.

The attribute spelling `[[clang::pre(...)]]` was considered as a second front
door for headers you do not own. **Cut.** It exists only to serve compilers that
do not implement the feature, which is the concern this appendix exists to
contain. Two front doors means two sets of tests, two sets of diagnostics, and a
real chance the uglier one becomes the default by accident. Everything
downstream is written against the AST rather than either spelling, so if a
concrete need for it ever appears it is a small addition then, not a v1
obligation now.
