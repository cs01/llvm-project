# Contracts for C in clang

A contract says what a function requires from its callers and what it guarantees
in return, written directly in the declaration. `-fc-contracts` type-checks it,
warns about the calls that violate it, and lowers it to
[CBMC](https://github.com/diffblue/cbmc) to be proved.

```c
int *allocate(unsigned long n)
  pre (n > 0);
```

A caller gets it wrong a thousand files away, and an ordinary build gives an
ordinary warning:

```
demo.c:6:12: warning: precondition n > 0 of 'allocate' is violated by this call [-Wcontract-violation]
    6 |   int *p = allocate(0);
      |            ^~~~~~~~~~~
demo.c:2:3: note: precondition declared here
    2 |   pre  (n > 0)
      |   ^~~~~~~~~~~~
```

There is no harness, no annotation at the call site, and no separate tool to
run. The comment that used to say `/* n must be positive */` now says it to the
compiler.

CBMC is already used in production: AWS runs it in CI on s2n-tls and
aws-c-common, FreeRTOS's TCP/IP stack is verified with it, and Kani, the Rust
verifier, is built on it.

> A branch of [cs01/llvm-project](https://github.com/cs01/llvm-project). The
> fork's other line of work, flow-sensitive nullability, is independent and lives
> on [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

## Installation

Build clang from this branch:

```sh
cmake -G Ninja -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_USE_LINKER=lld \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_OPTIMIZED_TABLEGEN=ON
ninja -C build clang
```

Proving anything also needs [CBMC](https://github.com/diffblue/cbmc) 6.x, with
`goto-cc` and `goto-instrument`. Ubuntu ships 5.95, whose loop-contract handling
differs, so take a release `.deb` from the CBMC repository instead.

## Usage

| Flag | What it does |
|---|---|
| `-fc-contracts` | enable the keywords. Without it they are ordinary identifiers |
| `-Wcontract-violation` | warn at call sites that provably break a precondition (on by default) |
| `-fcontract-runtime-checks` | check each precondition at function entry at run time |
| `-fcontract-emit-cprover` | print each function's contracts as CBMC clauses |
| `-fcontract-emit-cprover-unit` | rewrite the whole translation unit into CBMC form, ready for `goto-cc` |

`__has_feature(c_contracts)` is true under the flag, so a header can carry
contracts and still compile with a stock clang.

To see everything the extension does, end to end:

```sh
CLANG=build/bin/clang ./contracts-example/run.sh
```

That runs the examples below, plus
[`mistakes.c`](contracts-example/mistakes.c), which numbers every rule the front
end enforces, and the PCH round-trip.

`-fc-contracts` is C only, and rejects a C++ input rather than silently
ignoring the flag:

```
error: invalid argument '-fc-contracts' not allowed with 'C++'
```

`pre` and `post` are contextual keywords in the trailing position of a function
declarator, which in C++20 is where a `requires`-clause goes — so accepting the
flag there would change what valid C++ means rather than extend it. A C++
dialect has to align with P2900, which is why the keywords are already spelled
its way.

## Writing contracts

### Preconditions and postconditions

A `pre` is a condition the caller has to satisfy before calling. A `post` is
what the function guarantees when it returns:

```c
int *allocate(unsigned long n)
  pre  (n > 0)
  post (r: r != 0);
```

`r:` names the return value for this clause only. A caller that checks the
result for null is now re-checking something the callee already promised, and one
that skips the check is relying on the contract rather than guessing.

### Pointers and buffers

Almost every C function worth specifying takes a pointer and a length, and what
you need to say about them cannot be written in C itself:

```c
size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
  pre  (readable(src, srcSize))
  pre  (writable(dst, dstCap))
  post (r: r <= old(dstCap) || is_error(r));
```

`readable(p, n)` and `writable(p, n)` say the pointer is good for that many
bytes. `old(dstCap)` is the value at entry — required for a by-value parameter,
because C lets the body reassign it and the reader cannot tell which one you
meant.

There is a third one, `fresh`, and it is worth meeting early, because it and
`readable` both read as "this pointer is good for n bytes" while meaning
different things — and picking the wrong one decides whether a bug is found.

`readable(p, n)` is a *lower bound on the size of the object*: n bytes are
readable, and there may be more. Since it says nothing about the size above n, a
read at `p[n + 3]` verifies clean.

`fresh(p, n)` pins the size instead — a distinct object of exactly n bytes — so
the same read fails, because there is provably nothing there.

Proving a function safe usually wants `fresh`, since that is what catches the
function's own over-reads. A contract written for its callers usually wants
`readable`, since demanding an exact size asks for more than you mean.

### Frame conditions

A prover cannot verify a caller without knowing what a callee leaves alone, so
a frame condition is the clause that makes everything else composable:

```c
size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
  pre     (readable(src, srcSize))
  pre     (writable(dst, dstCap))
  assigns (((char *)dst)[0 : dstCap])
  post    (r: r <= old(dstCap) || is_error(r));
```

`buf[0 : n]` is a half-open range of *elements*; the compiler does the `sizeof`
multiply. Anything not named is guaranteed untouched — which is what lets a
proof about a caller use this contract instead of the function body.

This needs its own clause because a `post` cannot say it. "Nothing else changed"
would have to name every global and every object reachable through every
pointer, and say each one still equals its entry value. `assigns ()` — the empty
frame, modifying nothing — is the strongest one you can write, not an error.

The hazard is that a frame which is too *small* does not fail. It quietly proves
less: name half the buffer a function writes and the verifier will happily
discharge a weaker theorem than the one you meant. This is why a range is
counted in elements and the `sizeof` multiply is the compiler's job.

### Loops

A bounded checker unrolls a loop to some `--unwind N` and tells you the code is
right up to N. An invariant and a termination measure replace the bound with
induction, and the result holds for every length:

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

That is the difference between "tested harder than fuzzing" and "proved".

The loop needs a frame of its own for the same reason a function does: applying
an invariant means havocking whatever the loop writes and re-establishing the
invariant on top, so the prover has to be told what that is. Here it is the
counter and the buffer range.

### Keyword reference

There are six keywords. The full syntax and semantics, along with the four rules
that bite in practice — braced loop bodies, pure predicates, no restating a
contract on a redeclaration, and macro shadowing — are in
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
frame condition is a *set*, not a condition. A location may be a range:
`assigns (buf[0 : len])` is half-open and counted in **elements**, so the bound
is the one already in your loop header and the `sizeof` multiply CBMC needs is
the compiler's job.

These are *contextual* keywords, active only under `-fc-contracts`, so code
already using `pre` as an identifier keeps compiling.

For why these spellings rather than the verifier's `requires` and `ensures`, see
[contracts-design.md](contracts-design.md#5-syntax).

## How it works

Three checkers read the same contract, each catching what the one before it let
through: the **front end** (is the contract well formed?), the **call-site
pass** (does any caller break it?), and **CBMC** (is it true for every input?).
All three work today; the walkthrough with real diagnostics is in
[the reference](docs/contracts-reference.md#the-three-levels-in-detail).

Seven bug shapes, and how far up the ladder each survives. The first two are
malformed **contracts**; the rest are bugs in **code** carrying a contract like
this one:

```c
void put(int *buf, unsigned len, unsigned i, int v)   // does buf[i] = v
  pre  (buf != 0)
  pre  (i < len)
  assigns (buf[i]);
```

| The bug | Level 1<br>front end | Level 2<br>call-site | Level 3<br>CBMC |
|---|:---:|:---:|:---:|
| **Contract:** `post` names a mutated parameter without `old()` | **caught** | *n/a* | *n/a* |
| **Contract:** predicate calls an impure function | **caught** | *n/a* | *n/a* |
| **Code:** `put(b, 8, 8, 1)` — `len` and `i` are both 8, breaking `i < len` | missed | **caught** | caught |
| **Code:** `put(b, n, k, 1)` — the same bug, but `len` and `i` are variables | missed | missed | **caught** |
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

## Limitations

**The call-site warning stays quiet unless it can prove a violation.** Both of
these are silent:

```c
void use(int *p) pre (p != 0);

void caller(int c, int *maybe) {
  int *b = allocate(8);   // allocate's post says non-null ...
  use(b);                 // ... so this call is discharged

  int *p = maybe;
  if (c) p = 0;
  use(p);                 // the two edges disagree, so it says nothing
}
```

It reports only violations it can *demonstrate* — the difference between a
warning people leave on and one they turn off. The second call breaks the
precondition only when `c` is non-zero, and nothing at this level can decide
whether it ever is; the dataflow merges by keeping what every path agrees on, so
`p` becomes unknown rather than wrong.

Two other levels do reach it. CBMC decides it: verifying `caller` with
`goto-instrument --replace-call-with-contract use` asserts the callee's
precondition at the call site, and the solver returns the concrete `c` that
breaks it. `-fcontract-runtime-checks` reaches it at run time, for the inputs an
execution actually takes — `p != 0` is a scalar predicate, unlike the memory
clauses that tier declines. Neither is a substitute for the other: the warning is
free on every build, the solver is exhaustive but costs a harness.

A fuller example, including `old()` and the numbered list of every rule the front
end enforces, is in [`contracts-example/`](contracts-example/).

**Most proofs today are bounded**: exhaustive over a small domain rather than
universal. `ZSTD_wildcopy` is the exception and shows the route out.
[`proofs/zstd/UNBOUNDED.md`](proofs/zstd/UNBOUNDED.md) documents that route and,
more usefully, the five obstacles hit on the way, none of which is about
mathematics. That list is the useful scoping input: the hard part of applying
this to real C is toolchain-versus-codebase fit, not proving things.

**Nine checks a maintainer would demand before annotating their own code** are
written as an executable suite in [`proofs/zstd/e2e/`](proofs/zstd/e2e/), with
the failing ones recorded as failing on purpose. The one that decides whether
anyone can adopt this is the surface syntax: `pre (n > 0)` is a hard error under
stock gcc and clang, which is why the zstd patch wraps every clause in an
`#ifdef`.

## Results on real zstd

See **[proofs/zstd/](proofs/zstd/)**. Applied to the actual zstd sources, not a
transcription.

**Most of these were produced with hand-written CBMC harnesses, before this
extension could express them** — they are what motivated the grammar, not output
from it. The unbounded proof is the exception, and the one that matters: it now
runs from contracts written in this syntax.

### An unbounded proof, written in this grammar

`ZSTD_wildcopy`, in upstream zstd at `d9c0c7e2`, is memory-safe for **every**
length — `length` symbolic to 1 GiB, both buffers symbolically allocated, no
`--unwind` at all:

```
** 0 of 205 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

Proof by induction over the loop: 15 seconds, reproducible with
`./proofs/zstd/run-wildcopy-from-grammar.sh`. The frame the compiler generated is
byte-identical to the one a human wrote by hand after hitting five separate
obstacles. The earlier hand-written run also proves the fix for the
pointer-subtraction defect below: with it, zero failures; without it, exactly
one.

Two limits on that sentence, because "proved" should mean what it says:

- **It covers the `ZSTD_no_overlap` path.** That is the branch the harness
  exercises and the one the annotated loop is in. The short-offset
  `ZSTD_overlap_src_before_dst` path has its own `do { COPY8 } while` loop,
  which carries no contract — `goto-instrument` rejects loop contracts on a `do`
  loop — so nothing above says anything about it.
- **The source is rewritten, not just annotated.** The proof runs against a
  proof-only edit: the hot loop restructured from `do`/`while` to `while (1)`,
  `COPY16` inlined because `do { } while (0)` counts as a loop, and the `diff`
  computation moved inside the overlap branch where it is defined. Behaviour is
  identical and the patch says so at each point, but it is not upstream's text
  character for character.

### What else was found

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

None of them is a crash, and **fuzzing cannot find any of them**, because
nothing misbehaves at runtime. That, rather than the count, is the argument.
One more check — the `FSE_readNCount` bounds audit — came back clean, and the
negative result is recorded too.

Whether any of this actually makes C safer is tracked as a falsifiable
experiment with the bar written down, in
[`EXPERIMENT-annotation-yield.md`](proofs/zstd/EXPERIMENT-annotation-yield.md).
Findings that are real-but-unreachable, or that are really about the prover, are
logged as such. The count of new *reachable* UB findings the grammar has
produced so far is **zero**.

Read **[proofs/zstd/COST.md](proofs/zstd/COST.md)** before estimating anything:
solve time is driven by symbolic state size, not obligation count, and it decides
whether verifying a codec is a quarter or a research program.

## Roadmap

- **`when` guards on `assigns`.** A frame condition is a set of locations and a
  set cannot be disjoined, so `assigns (dst) when (r: !is_error(r))` is the only
  way to say "writes the output buffer on success, nothing on error".
  §4 item 5 of the design has the grammar.
- **Dogfood the grammar.** One proof now runs end to end from this syntax; the
  rest of `proofs/zstd/harnesses/` is still hand-written `__CPROVER_*` macros.
  The remaining gap is the harnesses themselves — `__CPROVER_assume`, symbolic
  allocation, the entry point — which this grammar does not try to express and
  probably should not: a contract belongs on the function, a harness is a proof
  driver.
- **Runtime checking at the call site.** `-fcontract-runtime-checks` already
  turns a `pre` into a check at function entry, calling a weak
  `__contract_violation(predicate, file, line, function)` that a project can
  replace with its own fault handler. What remains is the clauses about memory:
  those cannot be checked at entry, because C gives no way to recover an
  allocation from a pointer parameter, so the compiler declines them rather than
  emitting a check that would silently pass. The answer is to check them at the
  call site, where the caller still has the allocation in view — the same reason
  `_FORTIFY_SOURCE` checks at the call to `memcpy` rather than inside it, and the
  same place `-Wcontract-violation` already runs.
- **Contract inference**, so the first annotation on a large codebase is not
  hand-written from nothing.

## Documentation

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

## License

Apache License v2.0 with LLVM Exceptions, the same as the rest of the LLVM
project. See [LICENSE.TXT](LICENSE.TXT).

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
