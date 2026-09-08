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

## Result 0: the clause is what makes the CBMC proof go through

This is the one that answers the question. Same source, same harness, same
flags, `--enforce-contract BIT_getMiddleBits`; the only difference is whether
the one line of grammar is present.

```
=== with  pre(nbBits < 32)
** 0 of 2 failed (1 iterations)
VERIFICATION SUCCESSFUL

=== without pre(nbBits < 32)
[BIT_getMiddleBits.array_bounds.1] line 321 array 'BIT_mask' upper bound
    in BIT_mask[(signed long int)nbBits]: FAILURE
** 1 of 2 failed (2 iterations)
VERIFICATION FAILED
```

The clause lowers to `__CPROVER_requires`, `goto-instrument --enforce-contract`
turns it into the assumption the body is verified under, and that assumption is
exactly what bounds the `BIT_mask[nbBits]` index. Remove it and CBMC finds the
out-of-bounds read that [finding 6](../zstd/EXPERIMENT-annotation-yield.md)
describes.

**So the contract is doing the work, not decorating it.** The precondition had
to reach the prover somehow, and this is the path.

The honest qualifier: a `__CPROVER_assume(nbBits < 32)` in the harness would
produce the same verdict. What the clause buys over that is where the obligation
*lives*. In the harness it is one experiment's assumption, invisible to the
compiler and to the next person who writes a different harness. On the function
it is a property of the function: every harness inherits it, `-Wcontract-violation`
checks call sites against it, it survives `-DNDEBUG` (result 1), and a reader of
the header can see it. That is the difference between an assumption and a spec.

### Reproducing it

Two toolchain notes that cost more time than the experiment. `goto-cc` cannot
parse the arm64 SDK's `__mfp8` / `neon_vector_type` typedefs that zstd's
`compiler.h` drags in, so preprocess with `-U__ARM_NEON -U__ARM_NEON__` and
filter those lines -- the same class of problem as the `_Float128` note in
[`run-wildcopy-from-grammar.sh`](../zstd/run-wildcopy-from-grammar.sh), which is
about glibc. And the finding lives on the non-x86 path, so `-U__x86_64__` is
required or the table lookup is compiled out entirely.

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
| Contract clauses | 0 detected on their own; but they are what carries a precondition into CBMC (result 0), and the only thing that survives `-DNDEBUG` (result 1) |

The scanner is still ahead **at finding new defects**, and that is the honest
headline. But the comparison is not like for like: the scanner finds instances
of a pattern already known, and cannot say anything about a function it has not
seen the pattern in. Result 0 is the other job -- taking a function and
establishing it is correct for every input -- and there the clause is not
optional, it is the thing that carries the precondition to the prover.
