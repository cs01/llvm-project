# `inflate_table`'s doc-comment understates the table it needs

**Status:** documentation defect, confirmed by proof. **Not a bug in zlib** —
its own callers pass a large enough array. Anyone implementing against the
comment would get it wrong.

## What

`inftrees.c` documents the contract as:

> Build a set of tables to decode the provided canonical Huffman code.
> The code lengths are `lens[0..codes-1]`. **The result starts at `*table`,
> whose indices are 0..2^bits-1.**

Read as a caller must — "how big does my array have to be?" — that says
`2^bits` entries. It is not enough. `2^bits` is the size of the **root** table
only; any code longer than `bits` needs a *sub-table*, which `inflate_table`
allocates immediately after the root and indexes through the same pointer.

## The proof

[`harness_inflate_table.c`](harness_inflate_table.c) gives `table` exactly the
`2^bits` entries the comment promises, with `lens` fully symbolic and bounded
only by `MAXBITS`, which is all a caller decoding an untrusted stream can
assume. With `codes <= 5` and `bits = 3`:

| `table` size | result |
|---|---|
| 8 (`2^bits`, what the comment promises) | **3 of 330 failed** |
| 16 | 0 of 330 |
| 32 | 0 of 330 |
| 64 | 0 of 330 |

and the failures are not merely pointer formation:

```
line 243  dereference failure: pointer outside object bounds
          in next[(huff >> drop) + fill]
line 308  pointer arithmetic:  pointer outside object bounds in *table + used
```

Line 243 is a **write**. A caller who sized their array from the comment would
be corrupting memory past it.

## Why zlib is fine

Every in-tree caller passes `state->codes`, an array of `ENOUGH` entries —
`ENOUGH_LENS` is 852 and `ENOUGH_DISTS` is 592 — computed for the worst case
over all valid inputs. The bound is right; only the sentence describing it is
wrong.

This is the same shape as
[`BIT_lookBits`](../../zstd/findings/) in the zstd ledger: a documented
precondition weaker than the code's actual requirement, harmless today because
the only callers are in-tree, and a trap for the next caller. It is bucket 2,
and it is recorded as bucket 2.

## What the contract should say

```c
int inflate_table(codetype type, unsigned short *lens, unsigned codes,
                  code **table, unsigned *bits, unsigned short *work)
  pre (readable(lens, codes * sizeof(unsigned short)))
  pre (writable(work, codes * sizeof(unsigned short)))
  pre (writable(*table, ENOUGH * sizeof(code)))   /* not 2^bits */
  ...
```

The third line is the finding. It is one line, it is checkable, and it is the
line the comment does not say.
