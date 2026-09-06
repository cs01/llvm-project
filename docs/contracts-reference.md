# C contracts: reference

Full syntax, semantics and internals for the `-fc-contracts` extension. The
[README](../README.md) is the orientation; this is the detail.

## Contents

- [Grammar](#grammar)
- [`pre`, `post`, `old`](#pre-post-old)
- [`loop_invariant` and `decreases`](#loop_invariant-and-decreases)
- [The rules that will bite you](#the-rules-that-will-bite-you)
- [Flags](#flags)
- [How it works inside clang](#how-it-works-inside-clang)
- [The three levels, in detail](#the-three-levels-in-detail)
- [How CBMC fits](#how-cbmc-fits)
- [DO-178C / DO-333](#does-this-apply-to-do-178c--do-333-formal-methods)

## Grammar

Function clauses attach to the **declarator**, after the parameter list, on
either a prototype or a definition:

```
function-declarator:
        declarator '(' parameter-list ')' contract-clause-seq[opt]

contract-clause:
        'pre'      '(' expression ')'
        'post'     '(' result-binding[opt] expression ')'
        'assigns'  '(' assigns-target-list[opt] ')'

assigns-target-list:
        assignment-expression
        assigns-target-list ',' assignment-expression

result-binding:
        identifier ':'
```

Loop clauses sit between the loop header and the body:

```
iteration-statement:
        'while' '(' expression ')' loop-clause-seq[opt] compound-statement
        'for'   '(' ... ')'        loop-clause-seq[opt] compound-statement
        'do'    loop-clause-seq[opt] compound-statement 'while' '(' expr ')' ';'

loop-clause:
        'loop_invariant' '(' expression ')'
        'decreases'      '(' expression ')'
```

and `old` is an expression form, legal only inside a `post`:

```
old-expression:
        'old' '(' expression ')'
```

## `pre`, `post`, `old`

```c
unsigned long decompress(void *dst, unsigned long dstCap,
                         const void *src, unsigned long srcSize)
  pre  (dst != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap) || is_error((int)r));
```

Clauses repeat; each one is a separate obligation.

### The `r:` part — naming the return value

C has no way to *say* "the value this function returns". `return` is a
statement, and the result has no name you can write in an expression. So a
`post` that wants to constrain the result has to introduce a name for it, and
that is all the `r:` is:

```c
post (r: r <= old(dstCap))
//    ^  ^
//    |  the predicate, which may now use that name
//    the binding: "call the return value r for this clause"
```

The name before the colon is **declared** there; every use after the colon
refers to it. It is not a magic identifier — pick whatever reads best:

```c
unsigned long decompress(...) post (written: written <= old(dstCap));
int *allocate(unsigned long n) post (p: p != 0);
```

Two properties worth knowing:

- **It is optional.** A `post` that does not need to mention the result just
  omits it: `post (errno_is_clean())`. The `identifier :` prefix is only parsed
  when an identifier is immediately followed by a colon, so nothing is ambiguous.
- **It is scoped to its own clause.** The binding lives on the `ContractClause`,
  not on the function, so each `post` that wants the result names it again, and
  two clauses may use different names. This is also why the name cannot collide
  with anything: it does not outlive the parentheses it appears in.

Under `-fcontract-emit-cprover` the bound name is renamed to CBMC's
`__CPROVER_return_value`, which is the same concept with a fixed spelling. C++26
contracts (P2900) solve it the same way. The rename is whole-token, so a
parameter named `rate` is not mangled by a binding named `r`.

`old(dstCap)` names the value `dstCap` held at entry, and it is **required**
rather than optional. In C every parameter is a by-value copy that the body may
freely mutate (`src += 4`, `dstCap -= n` are ordinary in codec code), so a
`post` naming a bare parameter would be silently ambiguous between its entry and
exit value. Writing one is an error that tells you to use `old`. It takes
scalars only: snapshotting a struct is a copy, not an annotation.

## `loop_invariant` and `decreases`

An **invariant** is what stays true no matter how many times the loop runs. A
**variant** is what shrinks every time it runs. Together they are how a prover
handles a loop whose trip count it does not know, without unrolling it: the
invariant gives it induction, the variant gives it termination.

```c
void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    loop_invariant (i <= len)      // true before, and after every iteration
    decreases   (len - i)       // gets smaller each time, never negative
  {
    buf[i] = 0;
    i++;
  }
}
```

Read `decreases` as "the measure that runs out". `len - i` starts at `len`,
strictly decreases because `i` grows, and cannot go below zero because the
invariant bounds `i` by `len`. A quantity that can only decrease finitely many
times is a loop that must stop — that, and nothing more, is what a variant
proves. Leave it off and you can still prove the loop *correct*, just not that
it *ends*.

Without an invariant, a verifier's only options on a loop are to unroll it a
fixed number of times, which proves something about small inputs only, or to
give up and assume nothing. With one, `ZSTD_wildcopy` is proved memory-safe for
**every** length rather than up to some bound — see the zstd results below.

## The rules that will bite you

1. **A loop with clauses must brace its body.** `while (x) invariant(x);` is
   already a valid C call statement in some codebase somewhere, so the parser
   commits to the contract reading only when a `{` follows the clause sequence.
   Otherwise the tokens stay exactly what they were before this extension
   existed. The cost is that `for (...) loop_invariant (i < n) stmt;` is not a
   contract; brace it.
2. **Predicates must be pure.** No assignment, no `++`, and calls only to
   functions marked `const` or `pure`. Every tier evaluates a predicate more
   than once, so a predicate with a side effect is a predicate that means
   different things to different tiers. Those two existing GCC/clang attributes
   serve as the "usable in a spec" marker rather than a new one being invented.
3. **Contracts cannot be restated on a redeclaration.** Comparing two predicates
   written against two different sets of `ParmVarDecl`s for equivalence is not
   implemented, and silently keeping one of them would make *which declaration a
   caller happened to include* change what gets checked.
4. **A macro named `pre`, `post`, `loop_invariant`, `decreases` or `old` shadows the
   keyword**, and warns (`-Wc-contracts`) rather than erroring, because a
   project may have an unrelated `pre` macro and never write a contract.

## Flags

| Flag | Effect |
|---|---|
| `-fc-contracts` | turn the keywords on. Without it they are ordinary identifiers |
| `-fcontract-emit-cprover` | print the contracts as CBMC clauses on stdout |
| `-fcontract-emit-cprover-unit` | rewrite the whole translation unit into CBMC form, ready for `goto-cc` |
| `-Wcontract-violation` | the call-site violation warning (on by default, inside `-Wc-contracts`) |

`__has_feature(c_contracts)` is true under the flag, so a header can carry
contracts and still compile with a stock clang.

`-fc-contracts` is **C only**, and a C++ input is a hard error rather than a
silent no-op:

```
error: invalid argument '-fc-contracts' not allowed with 'C++'
```

The reason is a genuine collision rather than a scoping preference. `pre` and
`post` are contextual keywords in the trailing position of a function
declarator, which in C++20 is where a `requires`-clause goes. Accepting the flag
there would mean deciding, at that point, whether an identifier is a contract
keyword or the start of a constraint — so the flag would change what existing,
valid C++ means. A C++ dialect of this grammar has to align with P2900 instead,
which is why the keywords are already spelled its way.

## How it works inside clang

Four stages, each a real part of the compiler rather than a preprocessor trick.

**1. Parsed as contextual keywords.** `pre` and `post` are read in
`ParseFunctionDeclarator`, in the declarator suffix, with the parameters already
in scope — which is what lets a predicate name them. `post` is special-cased:
its predicate may bind the return value, and the return type is not known while
the declarator is still being built (for `int *f(void)` the pointer chunk is
added *after* the function chunk), so its tokens are saved and replayed once the
`FunctionDecl` exists. Loop clauses are read in `ParseStmt` between the header
and the body, using a token-stream lookahead — the parser scans balanced parens
past the clause sequence and only commits if a `{` follows.

**2. Type-checked and stored in the AST.** A `pre`, `post` or `loop_invariant`
predicate goes through the same contextual boolean conversion `if` uses, so
`pre (p)` on a pointer means `pre (p != 0)`. A `decreases` is a *measure*, not a
condition, so it keeps its own arithmetic type and is only required to be
scalar. Purity is enforced with `Expr::HasSideEffects`. Function clauses land in
a `ContractSpecifier` hanging off the `FunctionDecl` and survive a PCH; loop
clauses live in an `ASTContext` side table keyed by the loop statement, because
contracts are rare and `WhileStmt` is among the most numerous nodes in any AST —
paying eight bytes on every loop in every program to store nothing was the wrong
trade.

**3. Checked at compile time by a CFG dataflow pass.**
`clang/lib/Analysis/ContractChecking.cpp` is a standalone forward dataflow
analysis over clang's `CFG`, run from `AnalysisBasedWarnings` like any ordinary
warning:

```
demo.c:16:12: warning: precondition cap > 0 of 'buf_new' is violated by this call
demo.c:3:3:   note: precondition declared here
```

The lattice is deliberately tiny — a variable is a known integer, known null,
known non-null, or unknown. It walks blocks in reverse post-order, refines state
along each branch edge by the condition that got there, and **merges by keeping
only what every predecessor agrees on**: disagreement means the pass does not
know, and not knowing must never produce a report.

The sweep is iterated to a fixpoint, then a final pass reports from the
converged state. A single sweep was not merely imprecise, it was wrong: skipping
back-edge predecessors left a loop header holding the pre-loop state, which is
*stronger* than the truth, so a fact the body killed survived to the exit edge
and the pass invented reports —

```c
int n = 0;
for (int i = 0; i < 10; i++) n = i + 1;
f(n);      // n is 10; this was reported as violating pre (n > 0)
```

The lattice has height two and the merge only ever discards facts, so the
iteration is monotone and converges.

Constant arguments go through `Expr::EvaluateAsInt` rather than a hand-rolled
subset, so enum constants, casts, `sizeof`, arithmetic and file-scope
`static const` all fold. Variables whose address is taken are never tracked at
all, which is the cheap defence against the out-parameter false positive
(`T *p = 0; f(&p); p->x;`) that dominates this class of analysis. The function's own `pre` clauses are seeded as true on entry,
so a call inside the body can be discharged by a guarantee the caller already
made, and a callee's `post` is assumed after a call.

The pass reports only preconditions it can show are **violated** — never ones it
merely cannot prove. That is the difference between a warning developers leave
on and one they turn off.

**4. Emitted for CBMC.** `-fcontract-emit-cprover` prints the same clauses as
[CBMC](https://github.com/diffblue/cbmc) contracts, close to one for one, which
is the whole argument for targeting an existing verifier instead of building
one:

| Clause | Becomes |
|---|---|
| `pre (P)` | `__CPROVER_requires(P)` |
| `post (r: P)` | `__CPROVER_ensures(P)` with `r` renamed to `__CPROVER_return_value` |
| `old (e)` | `__CPROVER_old(e)` |
| `loop_invariant (P)` | `__CPROVER_loop_invariant(P)` |
| `decreases (m)` | `__CPROVER_decreases(m)` |
| `assigns (a, b)` | `__CPROVER_assigns(a, b)` |

```
$ clang -fc-contracts -fcontract-emit-cprover -fsyntax-only decompress.c
/* decompress */
__CPROVER_requires(dst != 0)
__CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap))
```

Function clauses are emitted from the declarator; loop clauses only once the
body has been parsed, since that is the first point they are reachable. The
effect is that a function's `pre` and `post` print ahead of the loops
they scope. `goto-instrument --enforce-contract` and `cbmc` then discharge them.

One back-end wart worth knowing: **goto-instrument rejects loop contracts on
`do` outright.** The grammar here deliberately keeps no such restriction, so the
emitter prints the clauses with a note that the loop needs the mechanical
`do { B } while (C)` → `while (1) { B; if (!C) break; }` rewrite first. It does
not perform the rewrite, because this mode emits clauses, not source.

### What each tier actually proves

Being precise about this matters more than the feature list:

- **The compile-time warning proves nothing.** It is unsound and incomplete by
  construction. It finds real bugs at call sites and is worth having on by
  default; it is not a guarantee.
- **CBMC proves the real thing**, but only what you asked: "given the
  preconditions, the postconditions hold and there is no UB, in this function."
  A wrong `post` is proved happily. It proves the code matches the spec, never
  that the spec is right.
- **There is no runtime-trap tier yet.** Nothing lowers a contract to a branch
  in codegen today. And note that the interesting clauses could not be traps
  anyway: `p != NULL` or `n > 0` compile to a branch fine, but "p points to at
  least n readable bytes" cannot, because there is no way to recover the
  allocation behind a `void *` at function entry. Buffer and frame clauses are
  documentation that the static tiers consume, not runtime checks.

See [`contracts-design.md`](../contracts-design.md) for the full design, the
rejected alternatives, and why the SMT-solver route inside clang was cut.

## The three levels, in detail

One contract, three levels of rigour. You choose how far up you go per function,
and each level catches what the one below it let through. Here is the same
buffer write at every level:

```c
void put(int *buf, unsigned len, unsigned i, int v)
  pre  (buf != 0)
  pre  (i < len)
  assigns (buf[i]);
```

### Level 1 — the front end: is the *contract* well formed?

Free, on every build, on every function. It says nothing about your program's
behaviour — only about the specification itself.

**Catches** a contract that cannot mean what it appears to:

```
error: 'post' predicate cannot name parameter 'cap' directly; a by-value
       parameter may be named in 'post' only through 'old()'
error: contract predicate must be free of side effects
note: mark 'impure' 'const' or 'pure' to allow calling it from a contract predicate
```

**Misses** everything about whether the code obeys the contract. `put(b, 8, 8, 1)`
compiles silently here.

### Level 2 — the call-site checker: does any *caller* obviously break it?

Still free, still an ordinary build, still no verifier. A CFG dataflow pass
looking at each call.

**Catches** what level 1 let through:

```
warning: precondition i < len of 'put' is violated by this call [-Wcontract-violation]
    7 |   put(b, 8, 8, 1);
note: precondition declared here
```

Constant arguments are folded by clang's own constant evaluator, so enum
constants, casts, `sizeof` expressions and arithmetic all reach the predicate,
not just literals.

**Misses** three things, all deliberately:

```c
put(b, n, k, 1);        // symbolic: it cannot relate n and k, so it says nothing
```

- **Anything symbolic.** The abstract domain is four values — known integer,
  null, non-null, unknown. Two unknowns have no relationship.
- **Anything a loop disagrees with itself about.** The dataflow runs to a
  fixpoint and merges by keeping only what every predecessor agrees on, so a
  variable the loop changes becomes unknown rather than wrong. That costs
  reports and never invents them.
- **The callee's own body.** It checks callers against a contract; it never asks
  whether `put` itself honours it.

That last one is the big gap, and it is not subtle:

```c
void fill(int *buf, unsigned len) pre (buf != 0) {
  for (unsigned i = 0; i <= len; i++)   // off by one
    buf[i] = 0;
}
```

Level 2 is silent on this. The bug is in the body, and the bound is symbolic —
both of its blind spots at once.

### Level 3 — CBMC: is it true for *every* input?

Costs a harness and solver time. In exchange it answers exhaustively rather than
for the cases you thought of.

**Catches** the `fill` off-by-one, for every `len`, by asking the solver whether
*any* input drives `buf[i] = 0` out of bounds — and returning the concrete one
that does. It also proves `put` writes nothing but `buf[i]`, which is what the
`assigns` clause is for. On real zstd this level found two undefined-behaviour
bugs that fuzzing cannot reach, because nothing misbehaves at runtime.

**Misses** a specification that is wrong. Write the off-by-one into the *contract*
instead of the code —

```c
  pre  (i <= len)     // wrong, but now it is the spec
```

— and CBMC proves the code matches it, cheerfully, forever. It also only sees
what your harness exercises, and an over-tight `__CPROVER_assume` narrows the
claim without telling you.

> Levels 1 and 2 above are real output from this branch. Level 3 is described
> rather than pasted, since CBMC runs separately; for actual CBMC transcripts see
> [`proofs/zstd/`](../proofs/zstd/).

## How CBMC fits

CBMC is the [C Bounded Model Checker](https://github.com/diffblue/cbmc) —
originally Daniel Kroening's, now largely maintained by AWS. It is **not** an
SMT solver; it is a verification engine that *drives* one:

```
  C source
     │  cbmc frontend
  goto program          loops become gotos; one IR
     │  symbolic execution, loops unrolled --unwind N
  SSA equations         r1 = (x0 < 0) ? -x0 : x0
     │  + the negated property:  ∃x. ¬(r1 >= 0)
  bit-blasted CNF       every int is 32 boolean variables
     │  SAT solver (CaDiCaL)
  SAT   → counterexample: concrete inputs that break it
  UNSAT → holds for every input, within the unroll bound
```

Worked example. `int abs32(int x) { return x < 0 ? -x : x; }`, claim
`abs32(x) >= 0`. Testing it on a thousand random values passes. CBMC asks the
solver whether *any* 32-bit `x` falsifies it and comes back SAT with
`x = INT_MIN`: `-INT_MIN` overflows back to itself in two's complement, so the
result is negative and the negation is UB besides. That is the difference from
fuzzing — it is exhaustive over the whole input space at once, symbolically.

**"Bounded" is the catch.** By default CBMC unrolls each loop a fixed number of
times, so you prove things only up to that bound. This is exactly what
`loop_invariant` and `decreases` are for: an invariant replaces unrolling with
induction, and a variant supplies termination, which together lift a proof from
"correct for n < 10" to "correct for every n". See
[`proofs/zstd/UNBOUNDED.md`](../proofs/zstd/UNBOUNDED.md) for a real one.

People do use it: AWS runs CBMC in CI on `aws-c-common` and s2n-tls, FreeRTOS's
TCP/IP stack has CBMC proofs, and Kani (the Rust verifier) is built on top of it.

Whether any of this can carry weight in certified avionics — DO-178C, the
DO-333 formal-methods supplement, DO-330 tool qualification — is answered
[below](#does-this-apply-to-do-178c--do-333-formal-methods).

## Does this apply to DO-178C / DO-333 formal methods?

The *shape* is right and the *stack* is not, and the gap is qualification rather
than mathematics.

DO-333, the Formal Methods supplement to DO-178C, explicitly contemplates
function-level formal specification replacing certain test objectives — which is
what contracts are. Two things stand between that and this stack, and neither is
the mathematics.

**Qualification.** Taking credit means the tool is qualified under **DO-330**,
and neither CBMC nor this clang fork is qualified, nor close to it. That is the
large one, and it is a programme of work rather than a fix.

**What a proof here still trusts.** The unwind bound is *not* the weak point it
is often assumed to be: `--unwinding-assertions` makes CBMC report when a bound
was too small rather than silently passing, and `loop_invariant` / `decreases`
replace the bound with induction outright — which is exactly what
[`UNBOUNDED.md`](../proofs/zstd/UNBOUNDED.md) demonstrates on `ZSTD_wildcopy`. What
is trusted is the *specification* and the *harness*: a wrong `post` is proved
happily, and an over-strong `__CPROVER_assume` silently narrows what was proved
without saying so. The call-site checker contributes nothing to credit either —
it reports only violations it can demonstrate and misses others by design, so
the absence of a warning is not evidence of absence.

Where it could genuinely help today is **development-time**: finding real defects
early and producing evidence that informs a formal-methods argument, not one that
discharges an objective. Worth noting that certified avionics C — MISRA-
constrained, no dynamic allocation, bounded loops — is a far friendlier
verification target than zstd is.
