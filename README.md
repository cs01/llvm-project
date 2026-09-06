# C contracts for clang

**Preconditions, postconditions and loop contracts for C as real compiler
grammar** — type-checked in the AST, checked at every call site by a CFG
dataflow pass, and lowered to CBMC so they can be *proved*. Not macros, not
comments, not `__attribute__` soup.

```c
unsigned long decompress(void *dst, unsigned long dstCap, const void *src)
  requires (dst != 0)
  requires (dstCap > 0)
  ensures  (r: r <= old(dstCap) || is_error((int)r));
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

**Write it once.**

```c
int *allocate(unsigned long n) requires (n > 0) ensures (r: r != 0);
```

**Get it checked in an ordinary build.** No harness, no separate tool, no proof:

```
demo.c:4:12: warning: precondition n > 0 of 'allocate' is violated by this call [-Wcontract-violation]
    4 |   int *p = allocate(0);
      |            ^~~~~~~~~~~
demo.c:1:32: note: precondition declared here
```

**And get it proved when it matters.** The same clause lowers to CBMC, which
answers for *every* input rather than the ones you thought to try:

```
$ clang -fc-contracts -fcontract-emit-cprover -fsyntax-only allocate.c
__CPROVER_requires(n > 0)
__CPROVER_ensures(__CPROVER_return_value != 0)
```

One source text, three levels of rigour, and you choose how far up you go per
function. That is the whole idea. Applied to real zstd it has already found two
undefined-behaviour bugs and proved the LZ reconstruction emits the right bytes
— [see below](#results-on-real-zstd).

Today the front end, the call-site checker and the CBMC export all work.
`assigns` and runtime trapping are next; see the [roadmap](#roadmap).

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

Five words. Full syntax and semantics in
**[docs/contracts-reference.md](docs/contracts-reference.md)**.

| Keyword | Goes | Says |
|---|---|---|
| `requires` | after a function's parameter list | must hold when the function is **called** |
| `ensures` | after a function's parameter list | must hold when it **returns**; `ensures (r: ...)` names the result |
| `old` | only inside an `ensures` | the value an expression had **on entry** |
| `loop_invariant` | between a loop's header and its body | true on entry and **preserved by every iteration** |
| `decreases` | between a loop's header and its body | **strictly decreases**, never negative — so the loop terminates |

`assigns` is reserved and hard-errors today. These are *contextual* keywords,
active only under `-fc-contracts`, so code already using `requires` as an
identifier keeps compiling.

Four rules bite in practice: a loop with clauses **must brace its body**;
predicates **must be pure** (calls only to `const`/`pure` functions); contracts
**cannot be restated on a redeclaration**; and a macro of the same name shadows
the keyword (with a warning). Details in the reference.

## Why a compiler, and not a header of macros

Fair challenge, and worth answering directly: the keywords are deliberately
CBMC's own names minus the `__CPROVER_` prefix, so why not
`#define requires(x) __CPROVER_requires(x)` and skip the compiler work?

Because two of the three tiers cannot exist in a header:

- **The call-site checker never touches CBMC.**
  [`ContractChecking.cpp`](clang/lib/Analysis/ContractChecking.cpp) is a CFG
  dataflow pass *inside clang* emitting ordinary warnings during a normal build.
  No verifier, no harness, no separate tool, no proof. A macro gets you none of
  it.
- **The clauses are type-checked, scoped, real AST.** `requires (dstCap > 0)` is
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

### Does this apply to DO-178C / DO-333 formal methods?

The *shape* is right and the *stack* is not, and the gap is qualification rather
than mathematics.

DO-333, the Formal Methods supplement to DO-178C, explicitly contemplates
function-level formal specification replacing certain test objectives — which is
what contracts are. But taking that credit means the tool is qualified under
**DO-330**, and neither CBMC nor this clang fork is qualified, nor close to it.
DO-333 also requires the analysis be *sound*: CBMC in bounded mode is not sound
for unbounded loops (hence the invariant work above), and the call-site checker
is unsound by construction, so it could never carry certification credit.

Where it could genuinely help today is **development-time**: finding real defects
early and producing evidence that informs a formal-methods argument, not one that
discharges an objective. Worth noting that certified avionics C — MISRA-
constrained, no dynamic allocation, bounded loops — is a far friendlier
verification target than zstd is.

## Results on real zstd

See **[proofs/zstd/](proofs/zstd/)**. Applied to the actual zstd sources, not a
transcription:

- **Two undefined-behaviour findings.** `ZSTD_overlapCopy8` forms a pointer up to
  8 bytes before the start of the output buffer, reachable with a legal stream
  (three-line fix, proved). `ZSTD_wildcopy` and `ZSTD_safecopy` subtract pointers
  their own doc-comments say are in different objects. Neither is exploitable on
  conventional hardware; both break CHERI and are freedoms a
  provenance-exploiting optimiser may take.
- **Functional correctness of the LZ reconstruction.** Not just "stays in
  bounds": `ZSTD_execSequence` provably emits the *right bytes*, including for
  matches that overlap their own output. This is the layer where "your data comes
  back" lives.
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

**`assigns`, the frame condition.** The single highest-value next piece. Every
hand-written annotation under `proofs/zstd/` carries an `__CPROVER_assigns`
frame that has no source syntax yet; the grammar is already pinned and
hard-errors, so the slot is reserved and waiting.

**Dogfood the grammar.** Everything under `proofs/zstd/harnesses/` is still
hand-written `__CPROVER_*` macros — the thing this extension exists to replace.
Writing those in this syntax and lowering them needs `assigns` above plus an
emit mode that produces a compilable translation unit rather than clauses on
stdout. Doing it turns every proof in that directory from a patch into source.

**Runtime trapping.** Turning `requires` into a deterministic trap at the
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
