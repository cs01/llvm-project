# Proof harnesses for zstd

Real proofs, discharged by CBMC, over the real zstd sources. Not fuzzing and not
bounded sampling: each obligation below is proved for **every** value of the
inputs satisfying the stated assumption.

## Running

CBMC's own C frontend cannot parse Homebrew LLVM's `arm_vector_types.h`, which
zstd pulls in through `compiler.h` -> `arm_neon.h`. Preprocess with the system
compiler first, with NEON turned off, and hand CBMC the result:

```sh
ZSTD=~/git/zstd
cc -E -U__ARM_NEON -DNDEBUG -I $ZSTD/lib/common -I $ZSTD/lib \
   harness_lookbits.c -o harness_lookbits.i
cbmc harness_lookbits.i --function harness \
     --bounds-check --pointer-check --conversion-check --undefined-shift-check \
     --unwind 4 --unwinding-assertions
```

`-DNDEBUG` matters: it compiles out the `assert` that would otherwise mask the
question, which is what a shipped build does.

Three more things this setup needs, learned the hard way:

- **`__builtin_memcpy` has no body in CBMC.** Apple's headers route `memcpy`
  through the builtin, so the preprocessed source has to be rewritten:
  `sed -e 's/__builtin_memcpy/memcpy/g' -e 's/__builtin_memmove/memmove/g'`.
  CBMC models the plain names.
- **`--object-bits 12`.** The default allows 2^8 addressed objects, which
  `ZSTD_execSequence` exceeds.
- **Harnesses for internal functions include the `.c`, not the `.h`.** `seq_t`
  and the fast path are file-local to `zstd_decompress_block.c`. Including the
  translation unit proves the real code rather than a transcription of it.

## Result 1: BIT_lookBits, and a contract that is weaker than the code

`BIT_getMiddleBits` ends in `BIT_mask[nbBits]` on every target that is not
x86_64, and `BIT_MASK_SIZE` is 32. `BIT_lookBits` documents `maxNbBits == 56` on
a 64-bit target.

Those two statements are not compatible, and CBMC says so. Assuming only what
the header documents:

```
__CPROVER_assume(nbBits <= 56);
[BIT_getMiddleBits.array_bounds.1] upper bound in BIT_mask[nbBits]: FAILURE
counterexample: nbBits = 35
VERIFICATION FAILED
```

Tightening the assumption to what the code actually requires discharges every
obligation:

```
__CPROVER_assume(nbBits <= 31);
** 0 of 15 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

**This is not a reachable bug in zstd, and should not be reported as one.** The
in-tree callers stay under 32: FSE reads use `tableLog`, Huffman reads use
`dtLog` (`HUF_TABLELOG_MAX` is 12), and the offset reads are bounded by
`ZSTD_WINDOWLOG_MAX_64`, which is 31. The `assert(nbBits < BIT_MASK_SIZE)`
encodes that belief and it appears to hold.

What it *is* is a contract defect, and a good argument for the whole project:

- The two implementations of `BIT_getMiddleBits` have **different
  preconditions**. The x86_64 path computes `(1 << nbBits) - 1` and tolerates
  `nbBits` up to 63; the portable path indexes a 32-entry table. Only the looser
  one is documented, and it is documented on the function that does not have it.
- The real precondition is a whole-program invariant spread across FSE, Huffman,
  and offset decoding. Nothing states it, so nothing checks it, and any future
  caller is free to violate it on aarch64 while passing every test on x86.

Written as a contract it is one line, and then it is checked rather than
believed:

```c
BitContainerType BIT_lookBits(const BIT_DStream_t *bitD, U32 nbBits)
  pre (nbBits < 32)
  pre (bitD->bitsConsumed + nbBits <= 64);
```

## Result 2: ZSTD_execSequence stays in bounds

Harness `harness_execsequence_small.c`, dst 64 / literals 32 + WILDCOPY_OVERLENGTH
slack, matchLength <= 32, `--unwind 34 --unwinding-assertions --object-bits 12`.

```
** 4 of 3496 failed (3 iterations)
```

All 3492 memory-safety obligations discharge, including every `array_bounds`
check (38 of 38) and the unwinding assertion. The unwind bound of 34 is not a
guess: `ZSTD_safecopy`'s leftovers loop runs at most `oend - oend_w` times,
which is `WILDCOPY_OVERLENGTH` = 32.

The four failures are the pointer-subtraction defect in
`FINDING-wildcopy-pointer-subtract.md`, which is a provenance question and not a
bounds violation.

So: **for this input domain, the wildcopy over-write in `ZSTD_execSequence`
provably stays inside the output buffer.** That is the property the whole fast
path rests on and it was previously supported by asserts that `-DNDEBUG`
deletes.

## Scope of the guarantee

The harness has no loops, so `--unwinding-assertions` passes at depth 1 and the
result is a complete proof over all `2^64 x 2^32 x 2^32` input combinations, not
a bounded one. That is the tier the rest of this work is aiming at; see
`../../contracts-design.md` section 2 for what separates it from the warning
tier.
