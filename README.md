# C contracts for clang

**Preconditions, postconditions and loop contracts for C as real compiler
grammar** — type-checked in the AST, checked at every call site by a CFG
dataflow pass, and lowered to CBMC so they can be *proved*. Not macros, not
comments, not `__attribute__` soup.

> A branch of [cs01/llvm-project](https://github.com/cs01/llvm-project). The
> fork's other line of work, flow-sensitive nullability, is independent and lives
> on [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

## One contract, three levels

Give C the ability to **say what a function requires and guarantees**, in the
declaration, in a form a maintainer will actually write — and then make that
statement worth something at three levels.

**Write it once, in the header**, where the function is already declared. `pre`
is what the caller must guarantee; `post` is what the function guarantees back,
with `r` naming the returned value.

```c
int *allocate(unsigned long n)
  pre  (n > 0)
  post (r: r != 0);
```

Then three checkers look at it, each catching what the one before it let through.

### Level 1 — the front end: is the *contract* well formed?

Free, on every build, on every function. It says nothing about your program's
behaviour, only about the specification itself — and a contract that cannot mean
what it appears to is a hard error, not a warning:

```
lvl1.c:3:12: error: 'post' predicate cannot name parameter 'n' directly; a by-value parameter may be named in 'post' only through 'old()'
    3 |   post (r: n > 0);
      |            ^
lvl1.c:3:12: note: name the value at function entry with 'old(n)'
```

### Level 2 — the call-site checker: does any *caller* break it?

Still free, still an ordinary build, still no verifier — a CFG dataflow pass
looking at each call. Somewhere far away, someone writes:

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

### Level 3 — CBMC: is it true for *every* input?

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

One source text, three levels of rigour, and you choose how far up you go per
function. That is the whole idea.

## Status

All three levels work today, and the CBMC output above has been run through
CBMC 5.95 end to end: contracts proved, and a deliberately false one correctly
rejected. Range targets in `assigns` are next — measured, not guessed, as
what blocks annotating real buffer code; see the
[roadmap](#roadmap).

The same three levels, against one buffer write, showing where each one stops:

```c
void put(int *buf, unsigned len, unsigned i, int v)
  pre  (buf != 0)
  pre  (i < len)
  assigns (buf[i]);
```

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

What each level catches and misses, with the real diagnostics, is in
[the reference](docs/contracts-reference.md#the-three-levels-in-detail).

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
frame condition is a *set*, not a condition.

These are *contextual* keywords, active only under `-fc-contracts`, so code
already using `pre` as an identifier keeps compiling.

Why these spellings and not the verifier's `requires` / `ensures`:
[contracts-design.md](contracts-design.md#5-syntax).

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

**Range targets in `assigns`.** There is no way to say "this range of memory" in
this grammar yet, and every real loop proof needs one — so this, not `when`, is
what actually blocks turning `proofs/zstd/harnesses/` into source. What CBMC
wants instead, and the open spelling question, are in
[§10 of the design](contracts-design.md#10-open-problems-not-yet-designed).

**`when` guards on `assigns`.** A frame condition is a set of locations and a
set cannot be disjoined, so `assigns (dst) when (r: !is_error(r))` is the only
way to say "writes the output buffer on success, nothing on error".
§4 item 5 of the design has the grammar.

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
