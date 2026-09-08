# What the contract clauses actually catch, measured on real zstd

Twelve findings are in [`FINDINGS.md`](../FINDINGS.md). Seven came from a Python
scanner, five from hand-written CBMC harnesses, **none from the extension**.
That is not a verdict on the extension, because until now nothing had asked it
to do the one thing a scanner cannot: relate a caller's obligations to a
callee's.

So: annotate the real thing and measure.

## The setup

[Finding 6](../zstd/EXPERIMENT-annotation-yield.md) is a contract mismatch, not
a pattern. `BIT_lookBits` documents `maxNbBits == 56` in a comment;
`BIT_getMiddleBits` requires `nbBits < 32`, stated as an assert. Both bounds
written in the grammar, on unmodified zstd:

```c
FORCE_INLINE_TEMPLATE BitContainerType BIT_getMiddleBits(..., U32 const nbBits)
    pre(nbBits < 32)

FORCE_INLINE_TEMPLATE BitContainerType BIT_lookBits(const BIT_DStream_t* bitD, U32 nbBits)
    pre(nbBits <= 56)
```

Compiled with `-fc-contracts -DNDEBUG`.

## Result 1: it catches what the release build stopped checking

```
tu2.c:3:61: warning: precondition nbBits < 32 of 'BIT_getMiddleBits' is
                     violated by this call [-Wcontract-violation]
    BitContainerType probe_direct(BitContainerType bc) { return BIT_getMiddleBits(bc, 0, 40); }
./lib/common/bitstream.h:308:5: note: precondition declared here
```

**`-DNDEBUG` is on.** zstd's own `assert(nbBits < BIT_MASK_SIZE)` is compiled
out of this build; nothing in a shipped zstd checks that bound. The `pre` clause
caught the violation anyway, at compile time, with no prover and no harness.

That is the extension doing something neither an assert nor a scanner does, and
it is the concrete answer to "is the syntax pulling its weight". A `pre` is a
release-build obligation; an `assert` is a debug-build one.
[Finding 7](../zstd/findings/FINDING-execsequence-implicit-preconditions.md) is
the same disease -- `ZSTD_execSequence`'s preconditions live entirely in asserts
that `-DNDEBUG` deletes -- so this is not a one-off.

## Result 2: it does *not* catch the finding it was pointed at

`BIT_lookBits(bitD, 40)` produces no diagnostic. 40 satisfies the caller's own
`pre(nbBits <= 56)`, and the call it makes,

```c
return BIT_getMiddleBits(bitD->bitContainer, ..., nbBits);
```

passes `nbBits` as a *parameter*, so the checker has no constant to evaluate and
stays quiet.

**The checker compares arguments against a precondition. It does not compare a
caller's precondition against a callee's.** Finding 6 is exactly that
comparison: `nbBits <= 56` is satisfiable with values that make `nbBits < 32`
false, so the two contracts are inconsistent independently of any call site or
any input.

## The feature this names

A check the front end could do with what it already has:

> For each call from `f` to `g` where an argument of `g` is a parameter of `f`,
> report when `f`'s precondition does not imply `g`'s precondition for that
> argument.

On the two clauses above that is `(nbBits <= 56) => (nbBits < 32)`, which is
false, and the counterexample is any `nbBits` in `[32, 56]`. It needs no solver
for the common case of interval bounds on an integer parameter, and it would
have found finding 6 mechanically instead of by a human reading two comments.

Recorded as a gap, not a defect: nothing claims this check exists. But it is the
strongest argument for the extension that has turned up so far, because it is a
class of bug that is **invisible to every other tool in this repository** -- no
sanitizer, no fuzzer, and no scanner can see a disagreement between two
documented bounds.

## Honest scoreboard

| Tool | Findings detected |
|---|---|
| `realloc-aliasing.py` | 7 |
| Hand-written CBMC harnesses | 5 |
| Contract clauses | 0 detected; 1 reproduced end to end; 1 release-build violation caught in this experiment |

The scanner is still ahead. But the scanner and CBMC are both available without
this fork, and neither can express result 1 or the check named above.
