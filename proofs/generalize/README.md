# Does this work outside zstd?

> The index of every finding, every clean result, and what has not been
> looked at is [`../FINDINGS.md`](../FINDINGS.md). Reproductions are in
> [`../repro/`](../repro/). Start there.

Everything under [`proofs/zstd/`](../zstd/) is one codebase, one domain, one
author's idioms. Two reachable defects came out of it, both of the same shape: a
pointer formed outside its object and never dereferenced. That shape is
invisible to fuzzers and to ASan, which is why it survives in mature code — but
it is also possible that what has been learned is how to find *zstd-shaped*
bugs.

So the same method is being pointed at other C, chosen for the property that
actually yields: **pointer arithmetic near a buffer boundary, on sizes an
attacker controls.**

| Project | Target | State |
|---|---|---|
| zlib | `inflate_table` (`inftrees.c`) | **bucket 2** — [doc understates the table size](zlib/FINDING-inflate-table-doc.md) |
| zlib | `inflate_fast` (`inffast.c`) | read, no defect found — see below |
| expat | `storeRawNames` (`xmlparse.c`) | **defect found**: [freed pointer read after realloc](expat/FINDING-storerawnames-freed-pointer.md) |
| sqlite | `fts3_unicode.c`, `fts5_hash.c` | **2 defects found**: [freed pointer read after realloc](sqlite/FINDING-realloc-freed-pointer-reads.md) |
| redis | `sds.c` realloc paths | read, no defect found: every path recomputes from the new pointer |
| jq | `jv.c` | swept by the detector, nothing; hand read not started |

## expat: the method found one in a second codebase

[`storeRawNames`](expat/FINDING-storerawnames-freed-pointer.md) repairs its
cached pointers *after* `realloc` has already freed the block they point into,
so it compares and subtracts an indeterminate pointer value. It is the same
class as the zstd findings, and survived for the same reason: nothing
dereferences the stale pointer, so ASan is silent and no fuzzer input
distinguishes a moved block from an in-place one.

This is the first finding from a codebase with no authorship or idiom overlap
with zstd, which is what the question at the top of this file was asking.

## And it reproduces through the branch's own grammar

Everything in this file was found with hand-written CBMC harnesses, which is
evidence about CBMC, not about this fork. Annotating expat `storeRawNames` with
`pre` clauses and lowering it with `-fcontract-emit-cprover-unit` gives the
identical result: same two properties, same 20740 obligations, on an 11k-line
real translation unit. See [the write-up](expat/RESULT-grammar-end-to-end.md),
including the two things it does *not* show: the clauses are not what detects
the defect, and `--enforce-contract` still crashes on this TU.

## And the finding generalises into a detector

The expat defect has a shape a script can look for: the argument a
`realloc`-shaped call later repairs from its own result, read in between.
[`scan-realloc-aliasing.py`](../scan-realloc-aliasing.py) does that, and is
validated by pointing it at libexpat, where it reports `storeRawNames` and
nothing else in 9436 lines.

Swept over redis, jq, sqlite and quickjs it found
[two more in sqlite](sqlite/FINDING-realloc-freed-pointer-reads.md): a pointer
subtraction whose operands both point into the freed block, and a hash-chain
walk that compares against the freed pointer. sqlite is plausibly the
most-tested C in existence, which is the useful part of the result: neither site
dereferences the stale pointer, so there is nothing for ASan to trap and nothing
for a fuzzer corpus to distinguish.

Everything else the sweep reported was sqlite's deliberate
`if( pNew==0 ){ free_the_old_one(); }` idiom, which is correct, since a failed
`realloc` leaves the old block alone. The detector does not model that guard;
its hits are triage, not verdicts.

**A null result is a result.** If ten annotated functions across four codebases
produce nothing, that belongs here in the same words as the findings — the whole
point of [the annotation-yield experiment](../zstd/EXPERIMENT-annotation-yield.md)
is that it can come back negative.

## zlib `inflate_fast`: read, and it is written correctly

The obvious candidate looked exactly like
[`ZSTD_overlapCopy8`](../zstd/findings/FINDING-overlapcopy8-oob-pointer.md) —
`from = out - dist`, with `dist` decoded from the stream. It is not a defect,
and the way it avoids being one is worth recording as the positive example.

zlib computes the available distance as an **integer** and checks it before
forming any pointer:

```c
op = (unsigned)(out - beg);     /* max distance in output */
if (dist > op) {                /* see if copy from window */
```

so the direct case at `from = out - dist` runs only when `dist <= op`, i.e.
`out - dist >= beg`. In the window branch, `out` has advanced by exactly
`dist - op` before `from = out - dist` is reached, so `from` lands on `beg`
precisely.

That is the fix zstd's `overlapCopy8` needs, already written, decades ago, in
the same kind of hot loop. The difference is not tooling — it is that one
codebase compares integers where the other subtracts pointers.

## zlib `inflate_table`: what is being asked

`inftrees.c` documents:

> The result starts at `*table`, whose indices are 0..2^bits-1.

zlib's own callers pass `state->codes`, an array of `ENOUGH` entries — 852 for
`LENS`, 592 for `DISTS` — which is far more than `2^bits` for any `bits` in use.
If the body can write past `2^bits`, then that sentence is not the contract the
code needs, and someone implementing against the comment would be wrong.

The contract in [`zlib/annotate-inflate-table.patch`](zlib/annotate-inflate-table.patch) gives `table`
exactly the `2^bits` entries the comment promises and nothing more, with `lens`
fully symbolic.

**It is not enough.** With `codes <= 5` and `bits = 3`, a `2^bits` table gives
`3 of 330 failed`, one of them a write through `next[(huff >> drop) + fill]`;
doubling it to 16 entries gives `0 of 330`. `2^bits` sizes only the *root*
table, and any code longer than `bits` needs a sub-table past it. zlib itself is
fine — every in-tree caller passes an `ENOUGH`-sized array — so this is bucket 2,
a documented contract weaker than the code needs. Full write-up in
[`FINDING-inflate-table-doc.md`](zlib/FINDING-inflate-table-doc.md).

That is the method reproducing on a second codebase, by the same route as the
zstd findings: write down what the documentation promises, and ask the prover
whether the body agrees.
