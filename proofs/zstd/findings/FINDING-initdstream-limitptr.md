# `BIT_initDStream` needs eight readable bytes, and its contract asks for one

**Status:** confirmed contract defect; undefined behaviour in any caller whose
buffer ends where `srcSize` ends. Not established as reachable from a crafted
zstd stream — that needs a caller-level proof, noted at the bottom as the open
question.

## What

`lib/common/bitstream.h`, `BIT_initDStream`:

```c
if (srcSize < 1) { ZSTD_memset(bitD, 0, sizeof(*bitD)); return ERROR(srcSize_wrong); }

bitD->start = (const char*)srcBuffer;
bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer);   /* <-- always +8 */

if (srcSize >= sizeof(bitD->bitContainer)) {  /* normal case */
    ...
} else {
    /* a dedicated switch over srcSize 7, 6, 5, 4, 3, 2 */
}
```

The `+ 8` runs before `srcSize` is compared against that same 8, and the `else`
branch exists precisely to serve `srcSize` of 1 through 7. So the function
supports a buffer of one byte and unconditionally forms a pointer eight bytes
into it.

C defines pointer arithmetic only within an object and one past its end
(C23 6.5.6p8), so on that path `limitPtr` is not a valid pointer value. It is
then used in relational comparisons in `BIT_reloadDStream`:

```c
if (UNLIKELY(bitD->ptr < bitD->limitPtr)) ...
if (bitD->ptr >= bitD->limitPtr) ...
```

which is a second, separate rule — relational comparison is defined only between
pointers into the same object (C23 6.5.9p6). Nothing is dereferenced, which is
why nothing misbehaves at runtime and why fuzzing does not reach it. It is the
same shape as [the `ZSTD_overlapCopy8` finding](FINDING-overlapcopy8-oob-pointer.md).

## The proof

[`harnesses/harness_initdstream.c`](../harnesses/harness_initdstream.c) allocates
the buffer at **exactly** `srcSize` bytes — what the doc-comment entitles a
caller to pass — and assumes nothing beyond `1 <= srcSize <= 8`.

```sh
cbmc harness_initdstream.i --function harness \
     --bounds-check --pointer-check --pointer-overflow-check --undefined-shift-check
```

```
[BIT_initDStream.pointer_arithmetic.5] pointer arithmetic:
    pointer outside object bounds in bitD->start + (signed long int)sizeof(BitContainerType)
    : FAILURE
** 1 of 248 failed (2 iterations)
```

Allocating the same buffer at `max(srcSize, 8)` — the precondition the body
actually needs — and changing nothing else:

```
** 0 of 248 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

One property, one variable, and it isolates the requirement exactly: **the body
needs eight readable bytes from `srcBuffer`, whatever `srcSize` says.**

`--pointer-overflow-check` is what makes this visible. `--pointer-check` alone
checks dereferences, and there is no bad dereference here; the defect is in a
pointer that is computed and compared but never followed. A check set that omits
it reports this function clean — see [COST.md](../COST.md).

## The contract, written down

The header today documents only that `srcBuffer` holds `srcSize` bytes. In this
fork's grammar the requirement the code has is one line, and it is a different
line:

```c
size_t BIT_initDStream(BIT_DStream_t* bitD, const void* srcBuffer, size_t srcSize)
  pre     (bitD != 0)
  pre     (__CPROVER_r_ok(srcBuffer, sizeof(size_t)))
  assigns (bitD[0 : 1])
  post    (r: r == old(srcSize) || ERR_isError(r));
```

which `-fcontract-emit-cprover` turns into

```c
__CPROVER_requires(bitD != 0)
__CPROVER_requires(__CPROVER_r_ok(srcBuffer, sizeof(size_t)))
__CPROVER_assigns(__CPROVER_object_upto(bitD, sizeof(*bitD)))
__CPROVER_ensures(__CPROVER_return_value == __CPROVER_old(srcSize)
                  || ERR_isError(__CPROVER_return_value))
```

Worth recording that the first draft of that `post` was written `r == srcSize`,
and the front end rejected it:

```
error: 'post' predicate cannot name parameter 'srcSize' directly; a by-value
       parameter may be named in 'post' only through 'old()'
```

`srcSize` is a by-value copy the body may mutate, so a bare mention is ambiguous
between its entry and exit value. That is level 1 of the ladder catching a
specification error in a document about specification errors, which is the
argument for type-checking these in the compiler rather than in a comment.

The second `pre` is the whole finding. It is not derivable from the first, it is
not what the doc-comment says, and writing it in the declaration is what made
the gap visible — the code reads as though the `else` branch handles short
buffers, and it does handle short *streams*; it does not handle short *objects*.

## What would fix it

Compute `limitPtr` only on the path that can use it:

```c
-    bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer);
     if (srcSize >= sizeof(bitD->bitContainer)) {  /* normal case */
+        bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer);
```

The short path sets `bitD->ptr = bitD->start` and is drained by
`BIT_reloadDStreamFast`'s `start` comparison rather than the `limitPtr` one, so
the value is dead there. This is not proposed as a patch: the point of the
finding is the missing precondition, and upstream may prefer to state the
stronger contract instead and leave the arithmetic alone.

## The open question

Whether a crafted stream reaches it depends on the caller's *object*, not on
`srcSize`. `BIT_initDStream`'s sub-buffer usually sits inside a larger frame
buffer, and `start + 8` then lands harmlessly in the bytes that follow. The
shape that bites is a short stream ending at the end of the caller's allocation.
The candidate is the four-stream Huffman path in `huf_decompress.c`, where
`length1..3` come from a 16-bit jump table read straight out of the stream and
stream 4 runs to the end of the buffer:

```c
size_t const length1 = MEM_readLE16(istart);
...
CHECK_F( BIT_initDStream(&bitD4, istart4, length4) );
```

with no lower bound on any of them. Worth noting that the *fast* variant of the
same path guards exactly this, and says so:

```c
/* HUF_initFastDStream() requires this, and this small of an input
 * won't benefit from the ASM loop anyways. */
if (length1 < 8 || length2 < 8 || length3 < 8 || length4 < 8)
    return 0;
```

So the requirement is known to the codebase; it is enforced in one path and not
in the other. Proving the second path reachable needs a harness over
`HUF_decompress4X1_usingDTable_internal_body` with a symbolic jump table, which
is not written yet.
