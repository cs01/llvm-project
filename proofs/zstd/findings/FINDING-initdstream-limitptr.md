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

The reproduction is a contract on the function
([`patches/annotate-initdstream.patch`](../patches/annotate-initdstream.patch),
zero `__CPROVER` tokens), which gives the buffer **exactly** `srcSize` bytes —
what the doc-comment entitles a caller to pass:

```c
c_pre (srcSize == 4)
c_pre (c_fresh(srcBuffer, srcSize))
c_pre (c_fresh(bitD, sizeof(BIT_DStream_t)))
```

`fresh` rather than `readable` is the load-bearing choice: `c_readable(p, n)` is a
*lower* bound, so CBMC may give the object slack past `n`, and that slack is
exactly what hides a pointer formed four bytes past the end.

Run it with [`../../repro/01-zstd-initdstream.sh`](../../repro/01-zstd-initdstream.sh),
which runs the control too:

```
[BIT_initDStream.pointer_arithmetic.5] line 272 pointer arithmetic:
    pointer outside object bounds in bitD->start + (signed long int)sizeof(BitContainerType)
    : FAILURE
** 1 of 27907 failed (2 iterations)          solved by sat in 6 s
```

Changing one clause to `c_pre (srcSize == 8)` — a buffer as long as the
bitContainer, which makes `start + 8` a legal one-past-the-end pointer — and
nothing else:

```
** 0 of 27907 failed (1 iterations)
VERIFICATION SUCCESSFUL                      solved by sat in 4 s
```

One property out of 27907, one clause changed, four seconds. An earlier
hand-written harness gave the same verdict over 248 properties; the contract
isolates it more sharply because the entry point is generated from the clauses
rather than written alongside them.

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
  c_pre (bitD != 0)
  c_pre (__CPROVER_r_ok(srcBuffer, sizeof(size_t)))
  c_assigns (c_range(bitD, 0, 1))
  c_returns (c_result == c_old(srcSize) || ERR_isError(c_result));
```

which `-fcontract-emit-cprover` turns into

```c
__CPROVER_requires(bitD != 0)
__CPROVER_requires(__CPROVER_r_ok(srcBuffer, sizeof(size_t)))
__CPROVER_assigns(__CPROVER_object_upto(bitD, sizeof(*bitD)))
__CPROVER_ensures(__CPROVER_return_value == __CPROVER_old(srcSize)
                  || ERR_isError(__CPROVER_return_value))
```

Worth recording that the first draft of that `c_post` was written `r == srcSize`,
and the front end rejected it:

```
error: 'post' predicate cannot name parameter 'srcSize' directly; a by-value
       parameter may be named in 'post' only through 'c_old()'
```

`srcSize` is a by-value copy the body may mutate, so a bare mention is ambiguous
between its entry and exit value. That is level 1 of the ladder catching a
specification error in a document about specification errors, which is the
argument for type-checking these in the compiler rather than in a comment.

The second `c_pre` is the whole finding. It is not derivable from the first, it is
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

## The caller's guards permit it, proved

The open question below was "do the caller's checks allow a stream shorter than
eight bytes at the end of the buffer?" They do, and
[`harnesses/harness_huf4x_stream4.c`](../harnesses/harness_huf4x_stream4.c)
produces the counterexample in 14 seconds against
`HUF_decompress4X1_usingDTable_internal_body` with `cSrc` allocated at exactly
`cSrcSize`:

```
[BIT_initDStream.pointer_arithmetic.5] pointer arithmetic:
    pointer outside object bounds in bitD->start + sizeof(BitContainerType): FAILURE
```

```
cSrcSize = 15                 valid offsets 0..14, one-past-end is 15
jump table  length1=2  length2=5  length3=1
length4 = 15 - (2 + 5 + 1 + 6) = 1

istart1 = +6    istart2 = +8    istart3 = +13   istart4 = +14
```

Every guard on that path passes: `cSrcSize >= 10`, `dstSize >= 6`,
`length4 > cSrcSize` is false, `opStart4 > oend` is false. Then
`BIT_initDStream(&bitD2, istart + 8, 5)` computes `start + 8 = istart + 16`,
one byte past one-past-the-end. Streams 3 and 4 are further out, at `+21` and
`+22`. The arithmetic checks by hand, which is the point of quoting it: the
result does not rest on trusting the tool.

The proof cuts the function off immediately after the four
`BIT_initDStream` calls, under `#ifdef ZSTD_REACH_PROBE`. Removing later code
is an under-approximation and cannot manufacture a trace, so the counterexample
is real; it is what took the run from a 2400-second timeout to 14 seconds.

## The caller chain, closed

`cSrc` is a sub-range in the library rather than its own allocation, so the
proof above left one question: does a real caller ever put a short stream at the
end of its *object*? It does, and the guard that allows it is a `>` that should
be a `>=`.

`ZSTD_decodeLiteralsBlock`, `zstd_decompress_block.c`:

```c
RETURN_ERROR_IF(litCSize + lhSize > srcSize, corruption_detected, "");
```

Strictly greater, so `lhSize + litCSize == srcSize` is accepted: the literals
section may end exactly where the caller's buffer ends. And in
`ZSTD_decompressBlock()` -- a public entry point -- that buffer is the user's
own object, passed straight down:

```
ZSTD_decompressBlock(dctx, dst, cap, src, srcSize)   src is the caller's object
  -> ZSTD_decompressBlock_deprecated
    -> ZSTD_decompressBlock_internal
      -> ZSTD_decodeLiteralsBlock(dctx, src, srcSize, ...)        line 2201
        -> HUF_decompress4X_usingDTable(..., istart + lhSize, litCSize, ...)
          -> BIT_initDStream(&bitD4, istart4, length4)
```

[`harnesses/harness_huf4x_subrange.c`](../harnesses/harness_huf4x_subrange.c)
models exactly that shape -- allocate the whole block, hand the decoder its tail
-- and fails in three seconds:

```
total = 34            object is 34 bytes, one-past-end is 34
lhSize = 2            cSrc = block + 2, cSrcSize = 32
jump table 6 / 12 / 2 so length4 = 32 - 26 = 6
istart4 = block + 28, and istart4 + length4 = 34, the end of the object

BIT_initDStream(&bitD4, block + 28, 6)  ->  limitPtr = block + 36
```

Two bytes past one-past-the-end. Every guard on the path passes:
`cSrcSize >= 10`, `dstSize >= 6`, `length4 > cSrcSize` false, `opStart4 > oend`
false. The arithmetic checks by hand.

The undefined behaviour happens **before** any error return. Even a block that
later fails sequence parsing has already formed the pointer.

### Scope, stated precisely

- **It is the block-level API.** `ZSTD_decompressBlock` is `ZSTDLIB_STATIC_API`,
  for advanced users, not the `ZSTD_decompress` frame path. Whether a frame can
  also place a literals section at its buffer's end is not established here:
  sequences and an optional checksum normally follow.
- **The Huffman table is modelled, not built.** The harness hands the decoder a
  well-formed X1 table descriptor rather than one decoded from the stream. The
  jump-table arithmetic and every guard are the real code; the table is an
  assumption.
- **Nothing is dereferenced.** Same severity class as
  [`ZSTD_overlapCopy8`](FINDING-overlapcopy8-oob-pointer.md): nothing misbehaves
  at runtime, which is why fuzzing does not reach it, and it is exactly the
  freedom a provenance-exploiting optimiser may take.

## What was open before that



`cSrc` is not its own object in the library. `ZSTD_decodeLiteralsBlock` passes
`istart + lhSize` with size `litCSize` -- a **sub-range of the caller's input
buffer**:

```c
hufSuccess = HUF_decompress4X_usingDTable(
    dctx->litBuffer, litSize, istart+lhSize, litCSize, dctx->HUFptr, flags);
```

So `start + 8` past the end of a short stream lands in the bytes that follow the
literals section, which are still inside the user's buffer, and no rule is
broken. Within a frame the sequences section follows the literals, so those
bytes normally exist.

What would close it is a caller whose literals section ends where the input
object ends. That is one more step, not a hand-wave, and until it is taken this
stays **bucket 2**: the contract is provably weaker than the code needs, and the
caller's own validation provably permits the shape, but no execution of a zstd
entry point has been shown to reach undefined behaviour.

## The original open question

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
