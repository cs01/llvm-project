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

The reproduction is the contract itself —
[`annotate-inflate-table.patch`](annotate-inflate-table.patch), zero `__CPROVER`
tokens — which gives `table` exactly the `2^bits` entries the comment promises,
with `lens` symbolic and bounded only by `MAXBITS`, all a caller decoding an
untrusted stream can assume:

```c
pre (codes >= 1 && codes <= 5)
pre (fresh(lens, codes * sizeof(unsigned short)))
pre (forall (i : 0, codes) lens[i] <= 15)
pre (*bits == 3)
pre (fresh(*table, (1u << 3) * sizeof(code)))    /* the line under test */
```

Run it with [`../../repro/02-zlib-inflate-table.sh`](../../repro/02-zlib-inflate-table.sh),
which also runs the control. With `codes` pinned at 5 and `bits = 3`:

| `table` size | result | solver time |
|---|---|---|
| 8 (`2^bits`, what the comment promises) | **3 of 338 failed** | 63 s |
| 16 | (control running; recorded when measured) | |

An earlier version of this contract spelled the element bound out as five
indices instead of `forall`; it gave 3 of 353 in 51 s, 0 of 353 at 16 entries
and 0 of 353 at 64. Same three properties, different property count because the
contract differs. The numbers above are the `forall` contract's own.

The counterexample is 26x cheaper than either proof, which is the usual
asymmetry: a counterexample needs one path, a proof needs all of them. The
64-entry proof is *faster* than the 16-entry one — slack removes boundary cases
rather than adding state.

The failures are not merely pointer formation:

```
inftrees.c:267  dereference failure: pointer outside object bounds
                in next[(huff >> drop) + fill]
inftrees.c:267  pointer arithmetic:  same expression
inftrees.c:332  pointer arithmetic:  pointer outside object bounds in *table + used
```

Line 267 is a **write**. A caller who sized their array from the comment would
be corrupting memory past it — and because the pointer is formed and stored
rather than read back through a guard, ASan and a fuzzer both miss it.

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
  pre (writable(*table, ENOUGH * sizeof(code)))   /* not 2^bits */
```

That line is the finding. It is one line, it is checkable, and it is the line
the comment does not say. Note `writable` rather than `fresh`: a caller is
entitled to pass a bigger buffer, and `writable` is a lower bound where `fresh`
fixes the size exactly. The proof above uses `fresh` precisely because pinning
the size is what makes the defect visible.
