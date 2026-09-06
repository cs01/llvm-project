# C contracts for clang

**Preconditions, postconditions and loop contracts for C as real compiler
grammar** — type-checked in the AST, checked at every call site by a CFG
dataflow pass, and lowered to CBMC so they can be *proved*. Not macros, not
comments, not `__attribute__` soup.

```c
unsigned long decompress(void *dst, unsigned long dstCap, const void *src)
  pre  (dst != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap) || is_error((int)r));
```

```c
void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    loop_invariant (i <= len)     // true every iteration
    decreases      (len - i)      // shrinks every iteration, so the loop ends
  { buf[i] = 0; i++; }
}
```

## The goal

C is not going anywhere. Billions of lines of it decode your video, compress
your backups and terminate your TLS, and the interesting bugs in that code are
not crashes — they are the ones where nothing misbehaves at runtime and the
answer is simply wrong. Fuzzing cannot find those. Tests cannot enumerate them.
Rewriting it all is not a plan.

So: give C the ability to **say what a function requires and guarantees**, in the
declaration, in a form a maintainer will actually write — and then make that
statement worth something at three levels.

### 1. Write it once, in the header

Where the function is already declared. `pre` is what the caller must
guarantee; `post` is what the function guarantees back, with `r` naming the
returned value.

```c
int *allocate(unsigned long n)
  pre  (n > 0)
  post (r: r != 0);
```

### 2. Every caller is checked in an ordinary build

No harness, no separate tool, no proof — an ordinary warning, from an ordinary
compile. Somewhere far away, someone writes:

```c
void setup(void) {
  int *p = allocate(0);      // 0 is not > 0
  ...
}
```

and the build tells them so, pointing at both the call and the promise it broke:

```
demo.c:6:12: warning: precondition n > 0 of 'allocate' is violated by this call [-Wcontract-violation]
    6 |   int *p = allocate(0);
      |            ^~~~~~~~~~~
demo.c:2:3: note: precondition declared here
    2 | pre (n > 0)
      |   ^~~~~~~~~~~~~~~~
```

### 3. And proved, when it matters

You never write a verifier's syntax by hand. Ask the compiler, and it translates
the contract you already wrote:

```
$ clang -fc-contracts -fcontract-emit-cprover -fsyntax-only allocate.c
__CPROVER_requires(n > 0)
__CPROVER_ensures(__CPROVER_return_value != 0)
```

Those `__CPROVER_` names are **[CBMC](https://github.com/diffblue/cbmc)'s own
spelling** for the same two ideas you wrote above — `__CPROVER_return_value`
being its name for `r`. That block is *generated output*, not something anyone
types. Hand it to CBMC and it proves the contract holds for **every** input, not
the handful a test happened to try.

---

One source text, three levels of rigour, and you choose how far up you go per
function. That is the whole idea. Applied to real zstd it has already found two
undefined-behaviour bugs and checked the LZ reconstruction emits the right bytes
— [see below](#results-on-real-zstd).

All three levels work today, and the CBMC output above has been run through
CBMC 5.95 end to end: contracts proved, and a deliberately false one correctly
rejected. `when` guards and runtime trapping are next; see the
[roadmap](#roadmap).

> A branch of [cs01/llvm-project](https://github.com/cs01/llvm-project). The
> fork's other line of work, flow-sensitive nullability, is independent and lives
> on [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

## Quick start

```sh
cmake -G Ninja -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_USE_LINKER=lld \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_OPTIMIZED_TABLEGEN=ON
ninja -C build clang
```

Then see everything the extension does, end to end:

```sh
CLANG=build/bin/clang ./contracts-example/run.sh
```

That runs the four demo files in [`contracts-example/`](contracts-example/):
[`contracts.h`](contracts-example/contracts.h) is the grammar,
[`checked.c`](contracts-example/checked.c) is compile-time call-site checking,
[`mistakes.c`](contracts-example/mistakes.c) is every rule the front end
enforces, numbered.

## The keywords

Six words. Full syntax and semantics in
**[docs/contracts-reference.md](docs/contracts-reference.md)**.

| Keyword | Goes | Says |
|---|---|---|
| `pre` | after a function's parameter list | must hold when the function is **called** |
| `post` | after a function's parameter list | must hold when it **returns**; `post (r: ...)` names the result |
| `old` | only inside a `post` | the value an expression had **on entry** |
| `loop_invariant` | between a loop's header and its body | true on entry and **preserved by every iteration** |
| `decreases` | between a loop's header and its body | **strictly decreases**, never negative — so the loop terminates |
| `assigns` | after a function's parameter list | the **only** locations the function may modify |

`assigns` takes a comma-separated list of locations rather than a predicate — a
frame condition is a *set*, not a condition.

The names follow one rule: **the standard where there is one, the older spec
languages where there is not.** `pre` and `post` are C++26 P2900's spelling, so
a C programmer meeting contracts elsewhere meets the same words. `old` predates
all of them — Eiffel, JML's `\old`, ACSL's `\old`. `assigns`, `loop_invariant`
and `decreases` are ACSL's, which CBMC then adopted.

These are *contextual* keywords,
active only under `-fc-contracts`, so code already using `pre` as an
identifier keeps compiling.

Four rules bite in practice: a loop with clauses **must brace its body**;
predicates **must be pure** (calls only to `const`/`pure` functions); contracts
**cannot be restated on a redeclaration**; and a macro of the same name shadows
the keyword (with a warning). Details in the reference.

## Three levels, and what each one misses

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

### The ladder, in one table

| The bug | Level 1<br>front end | Level 2<br>call-site | Level 3<br>CBMC |
|---|:---:|:---:|:---:|
| `post` names a mutated parameter without `old()` | **caught** | *n/a* | *n/a* |
| Predicate calls an impure function | **caught** | *n/a* | *n/a* |
| `put(b, 8, 8, 1)` — a literal breaks `i < len` | missed | **caught** | caught |
| `put(b, n, k, 1)` — symbolic arguments | missed | missed | **caught** |
| Off-by-one in the callee's own loop | missed | missed | **caught** |
| Violation only on a loop's second iteration | missed | missed | **caught** |
| **The contract itself is wrong** | missed | missed | **missed** |

*n/a* is not a miss: a malformed contract is a **hard error**, so the build stops
at level 1 and the later levels never run on that code.

That last row is the honest floor of the whole approach, and no tool on the
ladder fixes it: **verification proves the code matches the specification, never
that the specification is right.** What it buys you is that the specification is
now written down, in the declaration, where a reviewer can argue with it — which
is strictly more than a comment nobody checks.

> Levels 1 and 2 above are real output from this branch. Level 3 is described
> rather than pasted, since CBMC runs separately; for actual CBMC transcripts see
> [`proofs/zstd/`](proofs/zstd/).

## Why a compiler, and not a header of macros

Fair challenge, and worth answering directly: `assigns`, `loop_invariant` and
`decreases` are CBMC's own names minus the `__CPROVER_` prefix, so why not
`#define pre(x) __CPROVER_requires(x)`, a header of five macros, and skip the
compiler work entirely?

Because two of the three tiers cannot exist in a header:

- **The call-site checker never touches CBMC.**
  [`ContractChecking.cpp`](clang/lib/Analysis/ContractChecking.cpp) is a CFG
  dataflow pass *inside clang* emitting ordinary warnings during a normal build.
  No verifier, no harness, no separate tool, no proof. A macro gets you none of
  it.
- **The clauses are type-checked, scoped, real AST.** `pre (dstCap > 0)` is
  checked against the parameter's actual type; `old()` is scope-aware and
  scalar-restricted; the `r:` binding is a genuine `VarDecl`; purity is enforced
  via `Expr::HasSideEffects`. A macro is inert text until CBMC runs — and CBMC
  only looks at functions you wrote a harness for. The front end catches a
  malformed contract in *every* build, on *every* function.

Plus the practical one: `__CPROVER_*` in shipped source means vendoring CBMC
headers or `#ifdef` walls. A contextual keyword behind a flag doesn't perturb
the production build at all.

The CBMC export on its own really is a translation layer, and the design doc
says as much. What makes this a compiler feature rather than a header is the
other two tiers — and those are the ones every build gets, on every function,
whether or not anyone ever runs a prover.

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
[`proofs/zstd/UNBOUNDED.md`](proofs/zstd/UNBOUNDED.md) for a real one.

People do use it: AWS runs CBMC in CI on `aws-c-common` and s2n-tls, FreeRTOS's
TCP/IP stack has CBMC proofs, and Kani (the Rust verifier) is built on top of it.

Whether any of this can carry weight in certified avionics — DO-178C, the
DO-333 formal-methods supplement, DO-330 tool qualification — is answered in
[the reference](docs/contracts-reference.md#does-this-apply-to-do-178c--do-333-formal-methods).
Short version: the shape fits, the stack is not qualified, and qualification is
the blocker rather than the mathematics.

## Results on real zstd

See **[proofs/zstd/](proofs/zstd/)**. Applied to the actual zstd sources, not a
transcription.

**These were produced with hand-written CBMC harnesses, before this extension
could express them** — they are what motivated the grammar, not output from it.
Turning them into source in this syntax is [the roadmap's](#roadmap) real test.

- **An undefined-behaviour finding, reproduced on upstream HEAD.**
  `ZSTD_overlapCopy8` forms a pointer up to 8 bytes before the start of the
  output buffer, reachable with a legal stream. Verified against `d9c0c7e2`:
  `2 of 1601` properties fail before the three-line fix, `0 of 1601` after, same
  harness and flags. Not exploitable on conventional hardware — nothing
  misbehaves at runtime, which is why years of OSS-Fuzz have not surfaced it —
  but it is a freedom a provenance-exploiting optimiser may take.
- **A second, less confirmed one.** `ZSTD_wildcopy` and `ZSTD_safecopy` subtract
  pointers their own doc-comments say are in different objects. The subtraction
  is unconditional in HEAD, but it did **not** reproduce in the Linux/x86 run
  above, so treat it as likely rather than established.
- **An exhaustive correctness result for the LZ reconstruction.**
  `ZSTD_execSequence` emits the *right bytes*, including for matches that
  overlap their own output — but over a bounded domain (dst 48 bytes,
  `matchLength <= 16`), so this is exhaustive checking, not an unbounded proof.
  `findings/RESULT-lz-correctness.md` states the scope.
- **Two contract defects** where the real precondition is stronger than the
  documented one, or absent from where it is needed entirely.
- **An unbounded proof.** `ZSTD_wildcopy` is memory-safe for *every* length, not
  just up to some bound: `0 of 141 failed`, one iteration, no `--unwind` at all,
  length to 1 GiB with symbolically allocated buffers. Proof by induction over
  the loop, via a hand-written `__CPROVER_loop_invariant`. The same run also
  proves the fix for the pointer-subtraction defect: with it, zero failures;
  without it, exactly one.

None of the four is a crash, and **fuzzing cannot find any of them**, because
nothing misbehaves at runtime. That, rather than the count, is the argument.
A fifth check — the `FSE_readNCount` bounds audit — came back clean, and the
negative result is recorded too.

Read **[proofs/zstd/COST.md](proofs/zstd/COST.md)** before estimating anything:
solve time is driven by symbolic state size, not obligation count, and it decides
whether verifying a codec is a quarter or a research program.

## Roadmap

**Range targets in `assigns`.** Measured against CBMC, not guessed: a loop frame
of `assigns (i, buf[i])` is rejected, because the frame is evaluated at loop
entry so `buf[i]` denotes one element while the loop writes `buf[0..len)`.
CBMC wants `__CPROVER_object_upto(buf, n)`. There is no way to say "this range
of memory" in this grammar yet, and every real loop proof needs one — so this,
not `when`, is what actually blocks turning `proofs/zstd/harnesses/` into
source. The spelling is an open design question; §4 item 6 of the design already
contemplates `valid(p, n)`, and a slice form may be the better fit.

**`when` guards on `assigns`.** A frame condition is a set of locations and a
set cannot be disjoined, so `assigns (dst) when (r: !is_error(r))` is the only
way to say "writes the output buffer on success, nothing on error". Every
function in the target libraries behaves that way, so an unguarded frame lies on
half its executions. §5 of the design has the grammar.

**Dogfood the grammar.** Everything under `proofs/zstd/harnesses/` is still
hand-written `__CPROVER_*` macros — the thing this extension exists to replace.
Half of what blocked that is now done: `-fcontract-emit-cprover-unit` rewrites a
whole translation unit into CBMC form, and the result compiles under `goto-cc`
and discharges under `cbmc` (verified, both function and loop contracts). What
remains is range targets above, without which no buffer-touching frame can be
written at all.

**Runtime trapping.** Turning `pre` into a deterministic trap at the
earliest wrong state, for the clauses that can be branches. Worth noting that
buffer and frame clauses can never be traps — there is no way to recover the
allocation behind a `void *` at entry — so this tier is narrower than it sounds.

**Contract inference**, so the first annotation on a large codebase is not
hand-written from nothing.

### Known terrain

Most results today are bounded: exhaustive over a small domain rather than
universal. `ZSTD_wildcopy` is the exception and shows the route out.
`proofs/zstd/UNBOUNDED.md` documents that route and, more usefully, the four
obstacles hit on the way, none of which is about mathematics:

- CBMC rejects loop contracts on `do`/`while`; the loop must be rewritten.
- `do { } while (0)` macros count as loops, so a contract silently attaches to
  the wrong one. No diagnostic.
- The `assigns` clause havocs the cursors before the invariant is assumed, so
  raw pointer comparisons in an invariant get flagged themselves. Use
  `__CPROVER_same_object` and `__CPROVER_POINTER_OFFSET`.
- A symbolic extent in `assigns` generates its own unbounded havoc loop. Use a
  concrete bound where the semantics give you one.
- **`FORCE_INLINE` functions defeat callee contracts.** `ZSTD_wildcopy` is
  inlined into `ZSTD_safecopy` before contracts are applied, so the invariant
  written on the standalone function does not transfer. Loop contracts are per
  loop *instance*, so an inlined loop needs its invariant repeated at every site.
  The decoder has fifteen `FORCE_INLINE` uses, which multiplies the annotation
  burden rather than adding a fixed cost. This is the one that does not go away
  with a rewrite.

That list is the useful scoping input: the hard part of applying this to real C
is toolchain-versus-codebase fit, not proving things.

## Where to go deeper

- **[docs/contracts-reference.md](docs/contracts-reference.md)** — full grammar,
  semantics, and how the parser, AST, CFG pass and CBMC emitter actually work.
- **[contracts-design.md](contracts-design.md)** — the design argument: what was
  rejected and why, including the SMT-in-clang route that was cut.
- **[proofs/zstd/](proofs/zstd/)** — the proofs, sorted into `findings/`,
  `harnesses/` and `patches/`.
- **[contracts-example/](contracts-example/)** — runnable demos.

---

---

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
