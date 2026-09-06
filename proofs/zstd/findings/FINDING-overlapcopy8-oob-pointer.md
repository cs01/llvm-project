# `ZSTD_overlapCopy8` forms a pointer before the start of the object

**Status:** real, reachable with a legal stream, in the hot decode path.
Undefined behaviour per C 6.5.6p8. **Not exploitable on conventional hardware.**
Fix is three lines and is proved below.

## What

`lib/decompress/zstd_decompress_block.c`, `ZSTD_overlapCopy8`:

```c
static const int dec64table[] = { 8, 8, 8, 7, 8, 9,10,11 };   /* subtracted */
int const sub2 = dec64table[offset];
...
*ip += dec32table[offset];     /* forward 0..4 */
ZSTD_copy4(*op+4, *ip);
*ip -= sub2;                   /* back 7..11  <-- can land before the object */
...
*ip += 8;                      /* brought back in bounds */
```

`*ip` is left pointing up to 8 bytes before the start of the output buffer
between the subtraction and the later `+= 8`. C permits pointer arithmetic only
within an object or one past its end; forming the pointer is undefined, whether
or not it is dereferenced.

## Reachability

CBMC counterexample, under the decoder's real constraints including
`matchLength >= MINMATCH`:

```
sequence.litLength  = 10
sequence.matchLength = 3
sequence.offset      = 7      /* < WILDCOPY_VECLEN, so the overlap path is taken */
op == prefixStart             /* first sequence of the first block, no dictionary */
```

Then `match = oLitEnd - offset = dst + 3`, `dec32table[7] = 4` gives `dst + 7`,
and `sub2 = dec64table[7] = 11` gives **`dst - 4`**.

The general condition is just "the match target lies within the first 8 bytes of
the window", which any stream with an early short match satisfies. Nothing
exotic is required.

## Severity

Low, and worth stating precisely rather than inflating:

- The pointer is never dereferenced while out of bounds.
- On a flat address space the arithmetic produces the right value and the code
  works, which is why four years of fuzzing has not surfaced it: **nothing
  misbehaves at runtime**.
- It is exactly the freedom a provenance-exploiting optimiser is permitted to
  use. That is the argument that stands on its own.

- **Unverified:** an earlier draft claimed this breaks on CHERI, on the grounds
  that forming an out-of-bounds pointer invalidates the capability. That is
  probably wrong. CHERI Concentrate specifies a representable region wider than
  the object's bounds precisely so that transient out-of-bounds pointers in C
  keep their tag, and `dst - 4` is well inside it. The claim has not been tested
  on a CHERI toolchain and should not be repeated until it is; cheribuild plus
  QEMU would settle it.

## Fix, and its proof

`fix-overlapcopy8.patch`. Fold the subtraction into the trailing `+= 8` so the
pointer never leaves the object. The final value is unchanged:
`match + dec32 - sub2 + 8` and `match + dec32 + (8 - sub2)` are the same number.

Same harness, same flags, before and after:

```
before:  ** 6 of 4917 failed
         [ZSTD_overlapCopy8.pointer_arithmetic.65] pointer outside object bounds in *ip - sub2:  FAILURE
         [ZSTD_overlapCopy8.pointer_arithmetic.71] pointer outside object bounds in *ip + 8:     FAILURE
         + the 4 pointer-subtraction failures of FINDING-wildcopy-pointer-subtract.md

after:   ** 4 of 4804 failed
         only the 4 pointer-subtraction failures remain
```

## Reproducing

```sh
cc -E -U__ARM_NEON -DNDEBUG -I $ZSTD/lib/common -I $ZSTD/lib -I $ZSTD/lib/decompress \
   harness_execsequence_minmatch.c -o h.i
sed -e 's/__builtin_memcpy/memcpy/g' -e 's/__builtin_memmove/memmove/g' h.i > h2.i
cbmc h2.i --function harness --bounds-check --pointer-overflow-check \
     --unwind 34 --unwinding-assertions --object-bits 12
```

## Is this shape anywhere else?

Searched the whole tree for pointer decrements:

```sh
grep -n '\*ip -=\|\*op -=\|ip -= \|op -= \|match -= \|dst -= \|src -= ' \
     lib/decompress/*.c lib/common/*.h lib/compress/*.c
```

Two hits in all of zstd:

1. `zstd_decompress_block.c:820` — this bug.
2. `zstd_compress.c:6146`, `if (ip) ip -= zcs->stableIn_notConsumed;`

The second is **not** being reported as a bug. It is guarded by
`assert(input->pos >= zcs->stableIn_notConsumed)` one line above, and that
assertion is an invariant of the `ZSTD_bm_stable` input mode maintained by the
validation at `:6486`-`:6520`. It is worth recording only because the guard is
an `assert`, so under `-DNDEBUG` a caller that violates the stable-buffer
contract gets a `size_t` underflow on `input->pos` and a wild `ip`. That is API
misuse rather than an internal defect, but it is the kind of place a `pre`
clause would earn its keep, since the contract is currently enforced only in
debug builds.

The negative result is worth as much as the positive one: this bug class is rare
in zstd, so the finding is a genuine outlier rather than the first of many.
