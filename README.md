# Contracts for C in clang

Say what a function requires and guarantees, in the declaration. `-fc-contracts`
type-checks it, warns the callers who break it, and lowers it to
[CBMC](https://github.com/diffblue/cbmc) to be proved.

```c
int *allocate(unsigned long n)
  pre  (n > 0)
  post (r: r != 0);
```

Someone calls it wrong, a thousand files away. Ordinary build, ordinary warning:

```
demo.c:6:12: warning: precondition n > 0 of 'allocate' is violated by this call [-Wcontract-violation]
    6 |   int *p = allocate(0);
      |            ^~~~~~~~~~~
demo.c:2:3: note: precondition declared here
    2 |   pre  (n > 0)
      |   ^~~~~~~~~~~~
```

Ask for the proof form and the compiler writes the verifier's syntax for you:

```
$ clang -fc-contracts -fcontract-emit-cprover -fsyntax-only allocate.c
__CPROVER_requires(n > 0)
__CPROVER_ensures(__CPROVER_return_value != 0)
```

Loops take a frame, an invariant, and a termination measure — enough for CBMC to
prove a loop for *every* length instead of unrolling it to a bound:

```c
void zero(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns        (i, buf[0 : len])
    loop_invariant (i <= len)
    decreases      (len - i)
  { buf[i] = 0; i++; }
}
```

CBMC is not a research toy: AWS runs it in CI on s2n-tls and aws-c-common,
FreeRTOS's TCP/IP stack is verified with it, and Kani — the Rust verifier — is
built on it.

> A branch of [cs01/llvm-project](https://github.com/cs01/llvm-project). The
> fork's other line of work, flow-sensitive nullability, is independent and lives
> on [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

## What it does not warn about

As important as what it catches. Both of these are silent:

```c
int *b = allocate(8);   // post says non-null ...
put(b, 8, 0, 1);        // ... so this call is discharged

int *p = maybe;
if (c) p = 0;
put(p, 8, 0, 1);        // the two edges disagree, so it says nothing
```

It reports only violations it can *demonstrate* — the difference between a
warning people leave on and one they turn off.

A fuller example, including `old()` and the numbered list of every rule the front
end enforces, is in [`contracts-example/`](contracts-example/).

## The keywords

Six words. Full syntax, semantics, and the four rules that bite in practice —
braced loop bodies, pure predicates, no restating a contract on a redeclaration,
macro shadowing — in **[docs/contracts-reference.md](docs/contracts-reference.md)**.

| Keyword | Goes | Says |
|---|---|---|
| `pre` | after a function's parameter list | must hold when the function is **called** |
| `post` | after a function's parameter list | must hold when it **returns**; `post (r: ...)` names the result |
| `old` | only inside a `post` | the value an expression had **on entry** |
| `loop_invariant` | between a loop's header and its body | true on entry and **preserved by every iteration** |
| `decreases` | between a loop's header and its body | **strictly decreases**, never negative — so the loop terminates |
| `assigns` | after a function's parameter list | the **only** locations the function may modify |

`assigns` takes a comma-separated list of locations rather than a predicate — a
frame condition is a *set*, not a condition. A location may be a range:
`assigns (buf[0 : len])` is half-open and counted in **elements**, so the bound
is the one already in your loop header and the `sizeof` multiply CBMC needs is
the compiler's job.

These are *contextual* keywords, active only under `-fc-contracts`, so code
already using `pre` as an identifier keeps compiling.

Why these spellings and not the verifier's `requires` / `ensures`:
[contracts-design.md](contracts-design.md#5-syntax).

## Build it

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

That runs the examples above plus
[`mistakes.c`](contracts-example/mistakes.c), which is every rule the front end
enforces, numbered — and the PCH round-trip.

`-fc-contracts` is C only, and says so rather than ignoring you:

```
error: invalid argument '-fc-contracts' not allowed with 'C++'
```

`pre` and `post` are contextual keywords in the trailing position of a function
declarator, which in C++20 is where a `requires`-clause goes — so accepting the
flag there would change what valid C++ means rather than extend it. A C++
dialect has to align with P2900, which is why the keywords are already spelled
its way.

## What each level catches

Three checkers read the same contract, each catching what the one before it let
through: the **front end** (is the contract well formed?), the **call-site
pass** (does any caller break it?), and **CBMC** (is it true for every input?).
All three work today; the walkthrough with real diagnostics is in
[the reference](docs/contracts-reference.md#the-three-levels-in-detail).

Seven bug shapes, and how far up the ladder each survives. The first two are
malformed **contracts**; the rest are bugs in **code** carrying a contract like
this one:

```c
void put(int *buf, unsigned len, unsigned i, int v)
  pre  (buf != 0)
  pre  (i < len)
  assigns (buf[i]);
```

| The bug | Level 1<br>front end | Level 2<br>call-site | Level 3<br>CBMC |
|---|:---:|:---:|:---:|
| **Contract:** `post` names a mutated parameter without `old()` | **caught** | *n/a* | *n/a* |
| **Contract:** predicate calls an impure function | **caught** | *n/a* | *n/a* |
| **Code:** `put(b, 8, 8, 1)` — a literal breaks `i < len` | missed | **caught** | caught |
| **Code:** `put(b, n, k, 1)` — symbolic arguments | missed | missed | **caught** |
| **Code:** off-by-one in the callee's own loop | missed | missed | **caught** |
| **Code:** violation only on a loop's second iteration | missed | missed | **caught** |
| **Contract: well formed, and says the wrong thing** | missed | missed | **missed** |

*n/a* is not a miss: a malformed contract is a **hard error**, so the build stops
at level 1 and the later levels never run on that code.

That last row is the honest floor of the whole approach, and no tool on the
ladder fixes it: **verification proves the code matches the specification, never
that the specification is right.** What it buys you is that the specification is
now written down, in the declaration, where a reviewer can argue with it — which
is strictly more than a comment nobody checks.

What each level catches and misses, with the real diagnostics, is in
[the reference](docs/contracts-reference.md#the-three-levels-in-detail).

## Results on real zstd

See **[proofs/zstd/](proofs/zstd/)**. Applied to the actual zstd sources, not a
transcription.

**Most of these were produced with hand-written CBMC harnesses, before this
extension could express them** — they are what motivated the grammar, not output
from it. The unbounded proof below is the exception, and the one that matters:
it now runs from contracts written in this syntax.

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
- **Three contract defects** where the real precondition is stronger than the
  documented one, or absent from where it is needed entirely. The newest is the
  one that came out of *writing a contract* rather than reading code:
  `BIT_initDStream` computes `start + 8` before comparing `srcSize` against that
  same 8, on a path whose whole purpose is to serve `srcSize` of 1 through 7.
  With the buffer at exactly `srcSize` it is `1 of 248 failed`; at
  `max(srcSize, 8)`, `0 of 248`. One variable, so the missing precondition is
  isolated exactly.
- **An unbounded proof, written in this grammar.** `ZSTD_wildcopy` is
  memory-safe for *every* length, not just up to some bound: `0 of 208 failed`,
  one iteration, no `--unwind` at all, length to 1 GiB with symbolically
  allocated buffers. Proof by induction over the loop. The contracts are
  `assigns` / `loop_invariant` / `decreases` in upstream zstd source, lowered by
  this compiler — the generated frame is byte-identical to the hand-written one
  it replaces. `./proofs/zstd/run-wildcopy-from-grammar.sh` reproduces it in
  138 s. The earlier hand-written run also proves the fix for the
  pointer-subtraction defect: with it, zero failures; without it, exactly one.

None of them is a crash, and **fuzzing cannot find any of them**, because
nothing misbehaves at runtime. That, rather than the count, is the argument.
One more check — the `FSE_readNCount` bounds audit — came back clean, and the
negative result is recorded too.

Whether any of this actually makes C safer is being tracked as a falsifiable
experiment with the bar written down, in
[`EXPERIMENT-annotation-yield.md`](proofs/zstd/EXPERIMENT-annotation-yield.md).
Findings that are real-but-unreachable, or that are really about the prover, are
logged as such. The count of new *reachable* UB findings the grammar has
produced so far is **zero**.

Read **[proofs/zstd/COST.md](proofs/zstd/COST.md)** before estimating anything:
solve time is driven by symbolic state size, not obligation count, and it decides
whether verifying a codec is a quarter or a research program.

## Roadmap

**`when` guards on `assigns`.** A frame condition is a set of locations and a
set cannot be disjoined, so `assigns (dst) when (r: !is_error(r))` is the only
way to say "writes the output buffer on success, nothing on error".
§4 item 5 of the design has the grammar.

**Dogfood the grammar.** One proof now runs end to end from this syntax
(`run-wildcopy-from-grammar.sh`, above); the rest of `proofs/zstd/harnesses/` is
still hand-written `__CPROVER_*` macros. The remaining gap is the harnesses
themselves — `__CPROVER_assume`, symbolic allocation, the entry point — which
this grammar does not try to express and probably should not: a contract belongs
on the function, a harness is a proof driver.

**Runtime trapping.** Turning `pre` into a deterministic trap at the
earliest wrong state, for the clauses that can be branches. Worth noting that
buffer and frame clauses can never be traps — there is no way to recover the
allocation behind a `void *` at entry — so this tier is narrower than it sounds.

**Contract inference**, so the first annotation on a large codebase is not
hand-written from nothing.

### Known terrain

Most results today are bounded: exhaustive over a small domain rather than
universal. `ZSTD_wildcopy` is the exception and shows the route out.
[`proofs/zstd/UNBOUNDED.md`](proofs/zstd/UNBOUNDED.md) documents that route and,
more usefully, the five obstacles hit on the way, none of which is about
mathematics. That list is the useful scoping input: the hard part of applying
this to real C is toolchain-versus-codebase fit, not proving things.

## Where to go deeper

- **[docs/contracts-reference.md](docs/contracts-reference.md)** — full grammar,
  semantics, how the parser, AST, CFG pass and CBMC emitter actually work, what
  each level catches and misses, and how CBMC fits.
- **[contracts-design.md](contracts-design.md)** — the design argument: what was
  rejected and why, including why this is a compiler feature and not a header of
  five macros, and the SMT-in-clang route that was cut.
- **[proofs/zstd/](proofs/zstd/)** — the proofs, sorted into `findings/`,
  `harnesses/` and `patches/`.
- **[contracts-example/](contracts-example/)** — runnable demos.

Whether any of this can carry weight in certified avionics — DO-178C, the
DO-333 formal-methods supplement, DO-330 tool qualification — is answered in
[the reference](docs/contracts-reference.md#does-this-apply-to-do-178c--do-333-formal-methods).
Short version: the shape fits, the stack is not qualified, and qualification is
the blocker rather than the mathematics.

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
