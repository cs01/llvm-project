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
| 3 | A loop verifies without restructuring the function | fail |
| 4a | A callee's frame holds at the call site | **pass** |
| 4b | A callee's postcondition holds at the call site | fail |
| 5 | A contract on a FORCE_INLINE function reaches its call sites | fail |
| 6 | A wrong contract fails, loudly | **pass** |
| 7 | A proof fits in a CI step (60 s budget) | **pass**, 10 s |
| 8 | Annotations can live in the upstream source tree | fail |

Five of nine. The five that pass are not the easy ones: a wrong contract really
is rejected, the frame really does hold across a call replacement, and the
unbounded proof really does fit in a CI step. The five that fail are the ones
between a research artifact and something a maintainer would adopt.

**Case 8 is the one that decides whether anyone can use this at all**, and it
fails for a reason that is a syntax decision rather than a hard problem. Stock
gcc and stock clang were measured against four candidate spellings of the same
annotation:

| spelling | gcc 13 | stock clang |
|---|---|---|
| `pre (n > 0)` — what this fork parses | **error** | **error** |
| `__attribute__((contract_pre("n > 0")))` | ok, 1 warning | ok, 2 warnings |
| `[[clang::contract_pre(n > 0)]]` | ok, 1 warning | ok, 2 warnings |
| `/*@ pre n > 0; */` | ok, silent | ok, silent |

Only the first one cannot exist in a file that anyone else compiles, which is
why `patches/annotate-wildcopy-our-grammar.patch` wraps every clause in
`#ifdef ZSTD_CONTRACTS`. A maintainer will not take that, and the ifdefs are
not a presentation problem: they are the reason the annotations cannot go
upstream and therefore cannot be maintained by the people who own the code.

The AST, Sema checking, call-site analysis, serialization and CBMC lowering all
sit behind the parser and do not depend on the spelling. Changing the surface
syntax to one of the lower three rows is a parser change, not a redesign, and it
is the difference between a fork nobody installs and a tool that reads
annotations already sitting in someone's repository.

Case 4 is the one that decides whether this scales. Verification that must
inline every callee is bounded by the size of the whole program; verification
that can replace a call with its contract is bounded by one function at a time.
Everything else here is ergonomics by comparison.
