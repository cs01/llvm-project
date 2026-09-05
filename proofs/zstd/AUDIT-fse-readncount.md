# `FSE_readNCount` bounds audit: clean

Checked by reading, not by proof, because the answer turned out to be
structural. Recorded as a negative result.

## The question

`ZSTD_buildSeqTable`'s `set_compressed` path does not validate the symbol range
itself. It relies on `FSE_readNCount` to bound `max`, and the only thing between
that and `nbAdditionalBits[symbol]` is an `assert` at
`zstd_decompress_block.c:597`, which `-DNDEBUG` removes. `set_rle` by contrast
has an explicit `RETURN_ERROR_IF((*(const BYTE*)src) > max, ...)`.

So: can a corrupt stream drive `charnum` past `maxSV1` and either write out of
`normalizedCounter` or produce a `max` that indexes `OF_bits` out of bounds?

## Answer: no, and the code is careful about it

`entropy_common.c`:

- `charnum` advances by up to `3 * 12` per iteration in the repeat branch
  (`:90`), which looks alarming, but that branch **writes nothing**. The comment
  says why: "We don't need to set the normalized count to 0 because we already
  memset the whole buffer to 0."
- The only write, `normalizedCounter[charnum++]` at `:154`, is reachable only
  after a `charnum >= maxSV1` check on the previous iteration (`:114` for the
  repeat path, `:167` for the normal one), with `charnum` starting at 0. So the
  write always has `charnum <= max`.
- `:181` closes it: `if (charnum > maxSV1) return ERROR(maxSymbolValue_tooSmall);`
  before `*maxSVPtr = charnum-1`, so the returned max never exceeds the caller's.

For offsets `maxSV1` is 32 and `OF_bits` has 32 entries; the buffer is
`S16 norm[MaxSeq+1]` with `MaxSeq` = 52, against a worst-case `charnum` of 52.
Both fit exactly.

## Why this is worth recording

Two reasons. The `assert` at `:597` is doing no work in a shipped build, and the
property it asserts is maintained three functions away in a different file, so
this is another instance of the pattern in
`FINDING-execsequence-implicit-preconditions.md`. And the negative result is
data: this parsing layer is the most-fuzzed code in the project and it holds up
to inspection, which is a reason to expect a low bug-per-hour rate here rather
than a high one.
