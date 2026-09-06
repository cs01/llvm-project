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

## 2. `ZSTD_wildcopy`, proved from the grammar

**Not a bug. The dogfooding milestone.** The first proof on this branch whose
contracts were written in this syntax rather than in CBMC's macros.

`ZSTD_wildcopy` in upstream zstd `d9c0c7e2`, annotated with `assigns`,
`loop_invariant` and `decreases` (see
[`patches/annotate-wildcopy-our-grammar.patch`](patches/annotate-wildcopy-our-grammar.patch)),
lowered by `-fcontract-emit-cprover-unit`, and run through
[`run-wildcopy-from-grammar.sh`](run-wildcopy-from-grammar.sh):

```
** 0 of 208 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

No `--unwind`. One iteration. `length` symbolic to 1 GiB, both buffers
symbolically allocated. The generated frame is byte-identical to the
hand-written one it replaces.

Three things had to be true first, and two of them were bugs of mine rather
than facts about the tool — worth recording precisely, because each looked from
the outside like "CBMC cannot do this":

| Symptom | Looked like | Actually |
|---|---|---|
| 40 min, no result | tool limitation | `--pointer-overflow-check`, which the recorded recipe does not use |
| 50 min, no result | tool limitation | the lowering emitted `(p + 0)` and `* sizeof(char)`; CBMC carries the extent symbolically, so the identities cost real solver time |
| 50 min, no result | tool limitation | CBMC **5.95** from apt; every recorded time on this branch is **6.11** |

The middle one was a real defect in the emitter and is fixed. The other two are
now caught by the script rather than rediscovered: it asserts seven lowered
clauses, and refuses to run below CBMC 6.

**What this does and does not show.** It shows the grammar can express a real
unbounded proof over production C, which is what the branch existed to find out.
It does not show the grammar found anything: the invariants are the same ones a
human wrote by hand, transcribed into a syntax that type-checks them. The
bucket-1 count is unchanged.

## 3. `BIT_initDStream` forms a pointer eight bytes into a one-byte buffer

**Bucket 2 — bad spec, with a bucket-1 shape.** The defect is confirmed; whether
a crafted stream reaches undefined behaviour depends on the caller and is not
established. Full write-up:
[`findings/FINDING-initdstream-limitptr.md`](findings/FINDING-initdstream-limitptr.md).

`bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer)` runs before `srcSize`
is compared against that same 8, and the branch below it exists to serve
`srcSize` of 1 through 7. With the buffer allocated at exactly `srcSize` — what
the doc-comment entitles a caller to pass — and nothing assumed beyond
`1 <= srcSize <= 8`:

```
[BIT_initDStream.pointer_arithmetic.5] pointer arithmetic:
    pointer outside object bounds in bitD->start + sizeof(BitContainerType): FAILURE
** 1 of 248 failed
```

Allocating the same buffer at `max(srcSize, 8)` and changing nothing else takes
it to `0 of 248`. One variable, so the requirement is isolated exactly: the body
needs eight readable bytes whatever `srcSize` says, and the header promises
`srcSize`.

Three things this entry is worth more for than the finding itself:

- **The check set decided the outcome.** `--pointer-check` reports this function
  clean. The defect is in a pointer that is computed and compared but never
  dereferenced, so only `--pointer-overflow-check` sees it. The same run with
  `--conversion-check` instead produced two failures, both of them zstd's
  `ERROR()` convention (`(size_t)-1`) and neither of them UB. A finding count is
  a statement about the flags before it is a statement about the code.
- **The front end caught an error in the finding.** The first draft of the
  contract said `post (r: r == srcSize)`, and clang rejected it: `srcSize` is a
  by-value copy the body may mutate, so a bare mention is ambiguous between its
  entry and exit value. Level 1 catching a specification bug inside a document
  about specification bugs is the argument for type-checking contracts rather
  than writing them in comments.
- **The codebase already knows.** The *fast* four-stream path guards exactly
  this, with a comment saying why: `if (length1 < 8 || ... ) return 0;  /*
  HUF_initFastDStream() requires this */`. The requirement is enforced in one
  path and absent from the other, which is what an unwritten precondition looks
  like from the outside.

## Running tally

| # | Function | Bucket | Reachable |
|---|---|:---:|---|
| 1 | `BIT_lookBits` / `BIT_getMiddleBits` | 2 | no — 30 max vs 32 table |
| 2 | `ZSTD_wildcopy` | — | not a bug; first proof from the grammar |
| 3 | `BIT_initDStream` | 2 | shape of 1; caller reachability open |

Bucket 1 findings so far, from this experiment: **0**.
(`ZSTD_overlapCopy8`, the branch's one bucket-1 finding, predates it.)
