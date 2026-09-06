# `ZSTD_execSequence` reconstructs the right bytes

Not a bounds proof. This is functional correctness of the LZ reconstruction:
the layer where "your data comes back" actually lives.

## The specification

Written directly, as the LZ77 semantics rather than as a property of the code:

```c
__CPROVER_assert(
    __CPROVER_forall { unsigned i; i < litLength ==> dst[i] == lit[i] },
    "literals copied verbatim");

__CPROVER_assert(
    __CPROVER_forall { unsigned j;
        j < matchLength ==> dst[litLength + j] == dst[litLength + j - offset] },
    "match bytes equal the referenced bytes");
```

The second is deliberately self-referential. A match may overlap its own output,
which is the point of LZ77 and the reason `ZSTD_overlapCopy8` exists at all. A
specification that forbade the overlap would be proving the wrong thing.

## Result

```
[harness.assertion.1] literals copied verbatim:                 SUCCESS
[harness.assertion.2] match bytes equal the referenced bytes:   SUCCESS
** 4 of 3502 failed
```

The four failures are the pointer-subtraction defect in
`FINDING-wildcopy-pointer-subtract.md`. Neither functional assertion is among
them.

Domain: `dst` 48 bytes, literals 16 + `WILDCOPY_OVERLENGTH` slack,
`3 <= matchLength <= 16`, `litLength <= 16`, `offset >= 1`, prefix only, with
the preconditions from `FINDING-execsequence-implicit-preconditions.md`. Every
combination in that domain, exhaustively, including all overlapping-match cases
that route through `ZSTD_copy16`, `ZSTD_wildcopy`, `ZSTD_overlapCopy8` and the
`ZSTD_execSequenceEnd` slow path.

## Why this one matters more than the bounds results

Bounds proofs are a memory-safety property: the decoder does not scribble
outside its buffer. Useful, and the security tier. But a decoder that stays in
bounds and emits wrong-but-in-bounds bytes is a broken decompressor, and no
amount of bounds checking notices.

This is the first result that speaks to **data integrity**, and it is layer 1 of
the three-layer decomposition for round-trip correctness:

1. **Sequence execution: emitted sequences reconstruct the original bytes.**
   Proved here, for a bounded domain.
2. Entropy coding: `decode(encode(s)) == s`. Not started; the hard one, and
   likely needs an interactive prover rather than a bounded model checker.
3. Framing and block bookkeeping. Not started.

Plus the encoder-side invariant that makes the decomposition work at all: every
sequence the match finder emits is valid. That is a contract on the match
finder's output, and proving it never requires reasoning about *which* matches
the search chose, which is what keeps 8,400 lines of encoder heuristics out of
the proof entirely.

## Honest scope

Bounded, not universal. `matchLength <= 16` and a 48-byte output buffer, so this
is "exhaustive over a small domain", not "for all inputs". Widening is a
scheduling question, and `COST.md` has the measured curve: the cost is driven by
symbolic state size, so doubling the buffer is not free. An unbounded version
needs loop contracts, which is the next real step and the one where
`invariant` / `variant` stop being design.
