# What these proofs actually cost

Solve times matter more than any design argument here: they decide whether
verifying zstd is a quarter or a research program. Measured on an M-series
laptop, CBMC 6.11, single invocation.

| Target | Configuration | Obligations | Wall time |
|---|---|---|---|
| `BIT_lookBits` | loop-free, fully symbolic | 15 | seconds |
| `ZSTD_execSequence` | dst 48 / lit 16+32, match <= 16, unwind 12 | 3756 | ~100 s |
| `ZSTD_execSequence` | dst 64 / lit 32+32, match <= 32, unwind 24 | 3756 | ~10 min |
| `ZSTD_execSequence` | same, unwind 34 (the semantic bound) | 3756 | > 45 min, unfinished |
| `ZSTD_execSequence` | dst 96 / lit 64, match <= 96, unwind 40 | 3412 | > 35 min, abandoned |

## Which solver, and why it is not one answer

CBMC bit-blasts to a propositional formula and hands it to a built-in SAT
solver. It can hand the problem to an external SMT solver instead (`--z3`,
`--cvc5`, `--bitwuzla`, ...), and on this branch's harnesses that choice is
worth up to **20x in either direction**, depending on the harness.

Same instrumented goto program per row, one solver at a time, nothing else
running, Linux container with 4 cores, CBMC 6.11:

| Harness | Buffers | built-in SAT | z3 4.8.12 | cvc5 1.1.2 |
|---|---|---|---|---|
| `wildcopy`, loop contract, no unwind | `__CPROVER_allocate`, symbolic extent | 245 s | **13 s** | 14 s |
| `execSequence`, `--unwind 12` | fixed 96 / 64 / 64 arrays | **685 s** | > 1800 s | > 1800 s |
| `execSequence`, `--unwind 34` | same | **1640 s** | > 2700 s | > 2700 s |

Every finishing run agrees: same obligation count, same verdict.

**The rule, and it is mechanical:**

> If the buffer extents are **symbolic**, use `--z3`.
> If they are **constants** and you are unwinding, use the default.

The reason is the same fact from both sides. CBMC flattens arrays into
propositional variables. When an extent is a compile-time constant that
flattening is finite and well-structured, and unwinding piles up more of
exactly the concrete state a SAT solver is built to chew through -- so SAT wins,
and the SMT round-trip is pure overhead. When an extent is symbolic there is no
size to flatten to, the encoding is where the cost goes, and a solver with a
real theory of arrays never pays it.

Which means the two levers interact. Loop contracts remove the unwinding, and
removing the unwinding is what moves a harness from the second regime into the
first -- so the proofs that most want an SMT solver are exactly the ones that
just stopped needing a bound. It is worth checking both on any new harness; a
single run of each costs less than one wrong guess.

Two smaller results from the same sweep:

- **CaDiCaL is not the default, and is not free to select.** CBMC 6 ships it and
  `--sat-solver cadical` selects it, but on the wildcopy harness that took
  778 s against the default's 245 s. `--sat-solver minisat2` matched the default
  at 212 s, so the built-in path really is the MiniSat one the CBMC
  documentation describes. Do not infer a default from what is linked into the
  binary.
- **The speedup is not a weaker check.** On the `BIT_initDStream` harness, which
  fails, the default, `--z3` and `--cvc5` all report the same single property,
  by the same id, at `1 of 248`. A fast solver that agreed with a proof but not
  with a counterexample would be worth nothing, so this control is worth
  re-running whenever the flags change.

And an old solver was enough to see it: `z3 4.8.12` is Ubuntu 24.04's apt build,
several releases behind. The 20x did not need a current binary.

## Loop contracts change the shape of the cost, not just the size

Every row above is bounded: the price buys a result up to some `unwind`. Loop
contracts buy a different thing, and they are *cheaper*, because there is no
unwinding left to pay for. Measured in this repo's Linux CI container (4 cores),
CBMC 6.11:

| Target | Configuration | Obligations | Wall time |
|---|---|---|---|
| `ZSTD_wildcopy` | loop contract, no `--unwind`, length to 1 GiB | 208 | **245 s** SAT / **13 s** z3 |
| `ZSTD_wildcopy` | same, frame emitted as `(p + 0)` and `* sizeof(char)` | 208 | > 50 min, killed |

Same proof, same obligations, same answer. The second row is what this branch's
emitter produced before it was taught to canonicalise: a frame that denotes the
identical set of bytes, written with an additive and a multiplicative identity
left in. CBMC carries the extent symbolically into the havoc it generates, so
the identities are not folded away before they reach the solver — they widen the
expression the havoc loop is built from.

The lesson generalises past this one bug. **When a tool consumes generated text,
the generator's output is part of the cost model.** A lowering that is correct
but not canonical looks, from the outside, exactly like a tool that cannot
handle your program.

The other two apparent limits hit on the way to that number were also local:
`--pointer-overflow-check`, which the recorded recipe does not use (40 min, no
result), and CBMC **5.95** from Ubuntu's apt where loop-contract handling
differs from 6.x. Before concluding the prover cannot do something, check the
flags and the version — `run-wildcopy-from-grammar.sh` now pins both.

## Reading this

The cost is driven by the size of the symbolic state, not by the number of
obligations: the obligation count barely moves between rows that differ by an
order of magnitude in time. Doubling a buffer or raising an unwind bound is what
costs, because both multiply the array-theory terms the solver has to reason
about.

Two practical consequences:

- **Prove small, then widen.** A result on a reduced domain arrives in minutes
  and tells you the encoding and the assumptions are right. Widening is a
  separate, schedulable expense. Starting wide gives you nothing for an hour and
  then still nothing.
- **Pick loop bounds from the semantics, not by doubling.** The leftovers loop
  in `ZSTD_safecopy` runs at most `oend - oend_w` times, which is
  `WILDCOPY_OVERLENGTH` = 32. Reading that off the code gives 34 where guessing
  gave 70, and the difference is hours.

## The honest framing

Hours per function is not a failure, it is the normal shape of this work. AWS
runs CBMC proofs for s2n-tls and aws-c-common in CI on exactly this scale. The
mistake would be treating a long solve as a signal to give up rather than as a
job to schedule.

What it does mean is that **the per-function cost has to be measured before
anyone commits to a whole-codec timeline.** `BIT_lookBits` took an afternoon
including learning the toolchain. `ZSTD_execSequence` is the first function
where the solver, not the human, is the bottleneck. That is the number the
scoping decision should be built on.


## Check sets matter more than solve time

Every run before 2026-09-05 11:00 used `--bounds-check --pointer-check
--conversion-check` and nothing else. Adding `--signed-overflow-check
--unsigned-overflow-check --pointer-overflow-check` to the *same* harness on the
*same* code surfaced `FINDING-overlapcopy8-oob-pointer.md`, a real defect, in
code that had already been reported here as proved.

The lesson is not "run more checks", it is that a proof is only ever a proof of
the properties you asked about. Recording which flags produced a result is part
of the result.

Re-run under the full set, for the record:

| Target | Obligations | Result |
|---|---|---|
| `BIT_lookBits` (tight contract) | 18 | clean |
| `ZSTD_execSequence` | 4917 | 2 real failures + 4 known, now 4 after the fix |

## goto-synthesizer

CBMC ships a loop-invariant synthesizer, which is the intended route to
unbounded proofs over loops. On a toy harness it reports `result : PASS` and
emits a 178-byte stub, because its internal check does not use unwinding
assertions and so it sees nothing to strengthen. Parked rather than pursued: it
needs a case where the bounded proof genuinely fails first, and finding a real
one is worth more than making the tool work on a synthetic one.
