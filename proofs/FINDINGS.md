# Findings index

Everything found so far, what was checked and came back clean, and what has not
been looked at. **Read this before starting a new codebase** — the negatives are
here precisely so nobody spends a day re-deriving them.

Reproduce any of it with [`repro/`](repro/): `cd repro && ./run-all.sh`.

## Findings

| # | Where | What | Class | Repro |
|---|---|---|---|---|
| 1 | zstd `ZSTD_overlapCopy8` | forms a pointer up to 8 bytes **before** the output buffer | real, reachable UB | [finding](zstd/findings/FINDING-overlapcopy8-oob-pointer.md) |
| 2 | zstd `BIT_initDStream` | forms `start + 8` past the caller's object; reachable from public `ZSTD_decompressBlock` | real, reachable UB | [`01`](repro/01-zstd-initdstream.sh) |
| 3 | zlib `inflate_table` | doc says size the array `2^bits`; do that and the body **writes past it** | doc defect, proven | [`02`](repro/02-zlib-inflate-table.sh) |
| 4 | expat `storeRawNames` | compares and subtracts a pointer `realloc` already freed | real UB (indeterminate value) | [`03`](repro/03-realloc-aliasing-scan.sh) |
| 5 | sqlite `fts3_unicode.c` +1 more | same shape as 4 | real UB (indeterminate value) | [`03`](repro/03-realloc-aliasing-scan.sh) |
| 6 | zstd `BIT_lookBits` | documents a bound 26 wider than its callee accepts | doc defect, not reachable | [ledger](zstd/EXPERIMENT-annotation-yield.md) |
| 7 | zstd `ZSTD_execSequence` | preconditions live in asserts that `-DNDEBUG` removes | doc defect | [finding](zstd/findings/FINDING-execsequence-implicit-preconditions.md) |

**They are all one shape: a pointer that is formed, compared or subtracted, but
never dereferenced.** ASan instruments loads and stores and walks straight past
every one; no fuzzer input distinguishes the case that matters (whether
`realloc` moved the block, whether a stream sits at a buffer's end). That is why
they are still in shipped code, and it is the argument for a prover rather than
another sanitizer.

## Checked, nothing found — do not redo these

| Where | Why it looked promising | What is actually there |
|---|---|---|
| zlib `inflate_fast` | `from = out - dist`, `dist` straight from the stream — identical to finding 1 | **Correct, and it is the model.** zlib computes the distance as an *integer* (`op = (unsigned)(out - beg)`) and checks `dist > op` before forming any pointer; in the window branch `out` advances by exactly `dist - op` first, so `from` lands on `beg`. This is the fix finding 1 needs, written decades ago. |
| zstd `FSE_readNCount` | bounds-heavy header parsing of untrusted input | Clean. [audit](zstd/findings/AUDIT-fse-readncount.md) |
| zstd `ZSTD_wildcopy` | the unbounded proof target | Memory-safe for every length, `0 of 205`, no `--unwind`. [UNBOUNDED.md](zstd/UNBOUNDED.md) |
| zstd `ZSTD_safecopy` | second loop-bearing function in the decode path | Unproved, and the recorded reason (FORCE_INLINE) was **wrong** — see the note in UNBOUNDED.md. Its actual blocker is unestablished. |

## Scanner hits already triaged as false positives

[`scan-realloc-aliasing.py`](scan-realloc-aliasing.py) is a text-level filter
and says so. These are the hits it reports that are **not** defects, checked by
reading them. Do not re-triage them.

| Hit | Why it is fine |
|---|---|
| `zlib contrib/puff/pufftest.c:81` | the line is `buf = NULL;` — a *write* to `buf`, not a read of the freed value, and `free(buf)` precedes it deliberately |
| `zlib examples/enough.c:335` | `memset(vector + g.done[index].len, ...)` reads `.len`, a `size_t` member. The freed pointer is `.vec`, which is not read |
| `redis src/zmalloc.c:563` | `zmalloc_oom_handler(size)` reads `size`, an integer argument, not the reallocated pointer |

The pattern in all three: the scanner matches any read of a *name that shares a
prefix* with the reallocated pointer. Confirm every hit by reading it. Two of
seven were real.

## Not looked at yet

Ranked by the property that actually yields: **pointer arithmetic near a buffer
boundary, on sizes an attacker controls.**

- **redis `sds.c`** — cloned, scouted, no harness. `sdsHdrSize(s[-1])` recovers
  the allocation base from a type byte *stored in the buffer*, so every sds
  function carries an unwritten precondition: `s` points just past an intact
  header. Nothing in the source says it.
- **jq `jv.c`** — cloned, untouched. Refcounted values with pointer tagging.
- **zlib `inflate.c` `updatewindow`** — window wrapping, `put - state->wsize`.
- **libpng, brotli, lz4** — same family as zstd, not cloned.

## How to add to this

1. Pick a function that does pointer arithmetic near a boundary on
   attacker-controlled sizes.
2. Write its **documented** contract in this grammar — what the comment
   promises, not what the code does. That gap is the finding.
3. Prove with `--pointer-overflow-check`. Without it every finding above
   reports clean.
4. Run the control: allocate the buffer at the size the contract promises, and
   again at what the code needs. Two numbers, one variable.
5. Record it here **whichever way it falls.** A clean result saves the next
   agent a day, which is worth as much as a finding.

Bucket it honestly — 1 is reachable UB, 2 is a contract weaker than the code
needs, 3 is a limitation of the prover — in
[the ledger](zstd/EXPERIMENT-annotation-yield.md). Most findings are 2.
