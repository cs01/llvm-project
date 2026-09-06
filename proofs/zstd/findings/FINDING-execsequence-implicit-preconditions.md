# `ZSTD_execSequence` has preconditions that exist only in arithmetic elsewhere

**Status:** not a bug. All of these hold today. The point is that none of them is
stated where they are needed, and all of them are load-carrying.

## What the harness had to assume before the function was safe

Building a proof harness for `ZSTD_execSequence` means writing down what a
caller must guarantee. That list is not in the signature, not in a doc comment,
and not in any header. It was recovered by starting with nothing and adding an
assumption each time CBMC produced a counterexample:

```c
op != NULL
oend - op >= WILDCOPY_OVERLENGTH
sequence.matchLength >= 1
sequence.offset >= 1
sequence.offset <= (op - prefixStart) + sequence.litLength
litPtr + sequence.litLength <= litLimit
sequence.litLength + sequence.matchLength <= oend - op
/* and the one that is easiest to miss: */
at least WILDCOPY_OVERLENGTH readable bytes after litLimit
```

That last one is the interesting one. `ZSTD_copy16` unconditionally reads 16
bytes, and `ZSTD_wildcopy` over-reads up to `WILDCOPY_OVERLENGTH` past
`src + length`, both by design. So `ZSTD_execSequence` requires readable slack
after the literals buffer that nothing in its interface mentions.

It is satisfied. `zstd_decompress_block.c:107` says so in a comment:

> WILDCOPY_OVERLENGTH of buffer room to allow for overreads.

and the allocation arithmetic at `:86`, `:92` and `:113` maintains it. But the
guarantee is established in `ZSTD_decodeLiteralsBlock`, consumed in
`ZSTD_execSequence`, and connected by nothing a compiler can see.

## Why this is the finding rather than a footnote

Three results in one day, and all three have the same shape:

1. `BIT_lookBits` documents `maxNbBits == 56`; the portable path requires
   `nbBits < 32`. The real bound is a whole-program invariant across three
   decoders.
2. `ZSTD_wildcopy` computes `dst - src` in a case where its own doc-comment says
   the two pointers are in different objects.
3. `ZSTD_execSequence` needs `WILDCOPY_OVERLENGTH` of readable slack past the
   literals, established two call frames away.

None of these is a crash. Fuzzing will never surface any of them, because
nothing misbehaves at runtime. Every one is a contract that exists in the
authors' heads and in scattered arithmetic, and is therefore invisible to review
and unprotected against a future caller.

That is the argument for this project stated in evidence rather than in prose,
and it is what the `pre` clause is for:

```c
size_t ZSTD_execSequence(BYTE* op, BYTE* const oend, seq_t sequence,
                         const BYTE** litPtr, const BYTE* const litLimit, ...)
  pre (op != 0)
  pre (oend - op >= WILDCOPY_OVERLENGTH)
  pre (sequence.matchLength >= 1)
  pre (*litPtr + sequence.litLength <= litLimit)
  pre (sequence.litLength + sequence.matchLength <= (size_t)(oend - op));
```

Written once, checked at every call site, and lowered to `__CPROVER_requires`
for the proof. Today it is a comment in a different file.
