# Does this work outside zstd?

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
| zlib | `inflate_table` (`inftrees.c`) | harness written, solving |
| zlib | `inflate_fast` (`inffast.c`) | read, no defect found — see below |
| redis | `sds.c` header recovery | scouted, harness not written |
| jq | `jv.c` | not started |

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

[`zlib/harness_inflate_table.c`](zlib/harness_inflate_table.c) gives `table`
exactly the `2^bits` entries the comment promises and nothing more, with `lens`
fully symbolic. Result pending; it will be recorded here either way.
