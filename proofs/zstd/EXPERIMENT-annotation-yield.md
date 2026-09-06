# Does annotating real code find real bugs?

The honest question this branch has to answer. Annotate functions in zstd that
already have harnesses, and sort every finding into one of three buckets:

| Bucket | Meaning | Worth |
|---|---|---|
| **1. Real UB** | The code is wrong for inputs it can actually receive | Makes C safer |
| **2. Bad spec** | The documented precondition is wrong, missing, or inconsistent with a callee's | Makes C *clearer*; may prevent a future bug |
| **3. Tool limitation** | CBMC cannot express or see the property | Makes the *tool* better, not the code |

`FINDING-overlapcopy8-oob-pointer.md` is the template for bucket 1: real,
reproduced on upstream HEAD, three-line fix, proved.

**The bar:** if ten annotations produce only buckets 2 and 3, this is a learning
exercise rather than a safety tool, and the honest thing is to say so. Results
are recorded below whichever way they fall — the negative results are the point.

---

## 1. `BIT_lookBits` documents a bound 26 wider than its callee accepts

**Bucket 2 — bad spec.** Not reachable with valid inputs today. Zero margin.

`lib/common/bitstream.h`. `BIT_lookBits` documents its own contract in a comment:

```c
/*! BIT_lookBits() :
 *  On 32-bits, maxNbBits==24.
 *  On 64-bits, maxNbBits==56.        <-- the documented bound
 */
FORCE_INLINE_TEMPLATE BitContainerType BIT_lookBits(const BIT_DStream_t* bitD, U32 nbBits)
{
    return BIT_getMiddleBits(bitD->bitContainer, ... , nbBits);   /* passed straight through */
}
```

`BIT_getMiddleBits` states a different one, as an assert:

```c
    assert(nbBits < BIT_MASK_SIZE);          /* BIT_MASK_SIZE == 32 */
    ...
    return (bitContainer >> (start & regMask)) & BIT_mask[nbBits];   /* non-x86 path */
```

So **56 is permitted by the caller's documentation and forbidden by the
callee's**. On the non-x86 path the consequence is an out-of-bounds read of a
32-entry table. Confirmed with CBMC against a verbatim extract of the table and
the expression, assuming only `BIT_lookBits`' own documented bound:

```
[getMiddleBits.array_bounds.1] array 'BIT_mask' upper bound in BIT_mask[nbBits]: FAILURE
```

**Why it is bucket 2 and not bucket 1.** Every real caller is bounded well
below 32:

| Caller | Argument | Bound | Source |
|---|---|---|---|
| `HUF_decodeSymbolX1` and friends | `dtLog` | ≤ 12 | `HUF_TABLELOG_MAX` |
| `FSE_decodeSymbol`, `ZSTD_initFseState` | `tableLog` | ≤ 15 | `FSE_TABLELOG_ABSOLUTE_MAX` |
| `ZSTD_decodeSequence` | `ofBits` | ≤ 30 | `ZSTD_WINDOWLOG_MAX_64 - 1` |

Maximum real value 30, table size 32. It is correct today, by one index of
headroom, and for a reason stated nowhere near the code that depends on it.

**What a contract would have done.** `pre (nbBits < 32)` on `BIT_lookBits`
contradicts its own doc-comment on sight — the mismatch is visible at the
declaration rather than requiring a reader to follow a call into a different
header and know the size of a static table.

**Not being reported upstream.** No valid input reaches it, and "your comment is
wider than your table" is not a bug report. Recorded because the ledger is only
honest if the near-misses are in it.

---

## Running tally

| # | Function | Bucket | Reachable |
|---|---|:---:|---|
| 1 | `BIT_lookBits` / `BIT_getMiddleBits` | 2 | no — 30 max vs 32 table |

Bucket 1 findings so far, from this experiment: **0**.
(`ZSTD_overlapCopy8`, the branch's one bucket-1 finding, predates it.)
