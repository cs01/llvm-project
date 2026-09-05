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
