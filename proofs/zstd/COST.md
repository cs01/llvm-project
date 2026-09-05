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
