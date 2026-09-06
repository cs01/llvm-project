# `ZSTD_wildcopy` and `ZSTD_safecopy` subtract pointers into different objects

**Status:** real, low severity, two-line fix. Confirmed by two independent tools.
Not a memory-safety bug on any real target.

## What

`lib/common/zstd_internal.h`, `ZSTD_wildcopy`:

```c
void ZSTD_wildcopy(void* dst, const void* src, size_t length, ZSTD_overlap_e const ovtype)
{
    ptrdiff_t diff = (BYTE*)dst - (const BYTE*)src;   /* <-- unconditional */
```

and `lib/decompress/zstd_decompress_block.c`, `ZSTD_safecopy`:

```c
    ptrdiff_t const diff = op - ip;                    /* <-- unconditional */
```

C requires both operands of pointer subtraction to point into the same array
object, or one past its end. In the `ZSTD_no_overlap` case they deliberately do
not: the function's own doc-comment says "The source and destination are
guaranteed to be at least WILDCOPY_VECLEN bytes apart", and in
`ZSTD_execSequence` the call is

```c
ZSTD_wildcopy(op + 16, (*litPtr) + 16, sequence.litLength - 16, ZSTD_no_overlap);
```

where `op` is in the caller's output buffer and `litPtr` is in the decoder's
literals buffer: separate allocations.

`diff` is only *used* on the `ZSTD_overlap_src_before_dst` path and in an
`assert` that `-DNDEBUG` removes, so the computation is dead in exactly the case
where it is undefined.

## Evidence

CBMC, on the real `ZSTD_execSequence` via `harness_execsequence.c`:

```
[ZSTD_wildcopy.pointer.1]  line 220 same object violation in (BYTE *)dst - (const BYTE *)src: FAILURE
[ZSTD_wildcopy.overflow.1] line 220 arithmetic overflow on signed - in (BYTE *)dst - (const BYTE *)src: FAILURE
[ZSTD_safecopy.pointer.1]  line 843 same object violation in op - ip: FAILURE
[ZSTD_safecopy.overflow.1] line 843 arithmetic overflow on signed - in op - ip: FAILURE
```

AddressSanitizer agrees, on the reduced case in `pointer_subtract_repro.c`:

```
$ cc -fsanitize=address,undefined -fsanitize=pointer-subtract repro.c -o repro
$ ASAN_OPTIONS=detect_invalid_pointer_pairs=2 ./repro
ERROR: AddressSanitizer: invalid-pointer-pair: 0x000102ad4100 0x000102ad4160
  'dst_buf' ... 'src_buf'
```

Anyone running zstd under `detect_invalid_pointer_pairs=2` hits this.

## Why it does not bite today

Flat address spaces make the subtraction produce a meaningful number, and no
compiler currently exploits the provenance freedom here. The exposure is to
future optimisers, not to current ones.

## Fix

Compute `diff` only where it is defined and used:

```c
    if (ovtype == ZSTD_overlap_src_before_dst) {
        ptrdiff_t const diff = (BYTE*)dst - (const BYTE*)src;
        if (diff < WILDCOPY_VECLEN) { ... }
    }
```

## The point for this project

This is not the kind of bug fuzzing finds, because nothing misbehaves at
runtime. It is found by asking the compiler to prove a property, and it is
exactly what a `pre` clause would have stated: `ZSTD_wildcopy` has two different
contracts depending on `ovtype`, and only one of them permits the subtraction.
