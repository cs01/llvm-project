# What this has to do before anyone should adopt it

Each case below is a thing a maintainer of a real C codebase would require
before annotating their own code. They are written as executable checks, and
**several of them fail today on purpose**: a failing case here is a named
obstacle with a reproduction, which is worth more than the same sentence in a
roadmap.

Run them with `./run.sh`. The runner compares each case against its recorded
status and exits non-zero only when a case behaves differently from what is
written down — so a known-failing case turning green is a build failure, and
tells you to come update this file.

| # | What a maintainer wants | Today |
|---|---|:---:|
| 1 | Contracts cost nothing in a normal build | **pass** |
| 2 | The annotated source contains no prover vocabulary | **pass** |
| 3 | A loop verifies without restructuring the function | **pass** |
| 4a | A callee's frame holds at the call site | **pass** |
| 4b | A callee's postcondition holds at the call site | fail |
| 5 | A contract on a FORCE_INLINE function reaches its call sites | **pass** |
| 6 | A wrong contract fails, loudly | **pass** |
| 7 | A proof fits in a CI step (60 s budget) | **pass**, 10 s |
| 8 | Annotations can live in the upstream source tree | **pass** |
| 9 | A violated precondition can trap at runtime | **pass**, scalar `c_pre` |

Eight of nine. The cases that pass are not the easy ones: a wrong contract really
is rejected, the frame really does hold across a call replacement, and the
unbounded proof really does fit in a CI step.

**Case 8 is the one that decides whether anyone can use this at all**, and it
now passes through the portable `c_contracts.h` layer. A contract-aware compiler
expands `c_pre (n > 0)` to the checked grammar; GCC, stock clang and other C
compilers erase it. Both paths compile the same declaration, so an annotated
header can live upstream without a second source path.

## Case 9, and where a runtime check has to go

The tier that most potential users ask for first -- ship a checked build, trap
on a violated precondition, no prover in CI -- is easy for scalar clauses and
impossible in the obvious place for memory clauses. C gives no way to recover an
allocation's bounds from a `void *`, so `c_pre (c_readable(src, srcSize))` cannot be
checked where `src` arrives.

`__builtin_dynamic_object_size`, which is how `_FORTIFY_SOURCE` works, was
measured against the cases that matter:

| what the pointer is | recovered size |
|---|---|
| a fixed array | 16 |
| `malloc(40)` | 40 |
| `malloc(n)`, n symbolic | 7 |
| an interior pointer, `heap + 8` | 32 |
| **an opaque function parameter** | **-1, unknown** |

The last row is the design. Object size is available at the *allocation* site and
gone by the *callee*, so a check emitted in the function prologue can only ever
cover scalars. Emitted at the **call site** it usually succeeds, because the
caller still has the allocation in view -- which is exactly why `_FORTIFY_SOURCE`
checks at the call to `memcpy` rather than inside it.

That is also where the call-site checker already runs. The static tier warns
when it can prove a violation; a runtime check traps when it cannot decide
statically but the size is live at run time. One mechanism, one location, two
tiers -- rather than a third thing bolted to the prologue that silently skips
every clause about memory.

Case 4 is the one that decides whether this scales. Verification that must
inline every callee is bounded by the size of the whole program; verification
that can replace a call with its contract is bounded by one function at a time.
Everything else here is ergonomics by comparison.

**Not yet covered: the third direction of case 4.** 4a and 4b ask what a caller
may *assume* from a callee's contract — the frame, then the postcondition. The
opposite direction, whether replacing a call asserts the callee's
*precondition* at the call site, is the one the README's level-3 column rests on:
it is what catches a violation the call-site warning declines to report, such as
a pointer that is null on only one path. `--replace-call-with-contract` is
specified to assert it, and nothing here runs that yet. Given 4b already fails,
it should not be assumed to work until a case demonstrates it.
