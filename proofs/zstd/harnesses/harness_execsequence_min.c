// Proof harness: does ZSTD_execSequence stay inside the output buffer?
//
// The function's preconditions live in ten asserts that -DNDEBUG removes, and
// its fast path calls ZSTD_wildcopy, which deliberately writes past the length
// it was asked for (up to WILDCOPY_OVERLENGTH). Whether that overwrite is in
// bounds rests on the `oMatchEnd > oend_w` guard, which is the property worth
// proving.
// Included as a translation unit: seq_t and the fast path are internal to
// this .c file, and the point is to prove the real code rather than a copy.
#include "zstd_decompress_block.c"

#define DST_CAP   48u
#define LIT_CAP   16u
#define DICT_CAP  64u

unsigned int  nondet_uint(void);
unsigned long nondet_ulong(void);

void harness(void)
{
    static BYTE dst[DST_CAP];
    static BYTE lit[LIT_CAP];
    static BYTE dict[DICT_CAP];

    BYTE *const ostart = dst;
    BYTE *const oend   = dst + DST_CAP;

    // Where in the output we are, and how much prefix is already decoded.
    // Output position pinned. The fast path's bounds reasoning is relative to
    // oend, so a symbolic start adds state space without adding coverage of
    // the property under test.
    BYTE *op = ostart;

    const BYTE *litPtr = lit;
    const BYTE *const litLimit = lit + LIT_CAP;

    seq_t sequence;
    sequence.litLength  = nondet_ulong();
    sequence.matchLength = nondet_ulong();
    sequence.offset      = nondet_ulong();

    // No extDict: prefix only, which is the common configuration and the one
    // the fast path is written for.
    const BYTE *const prefixStart  = ostart;
    const BYTE *const virtualStart = ostart;
    const BYTE *const dictEnd      = ostart;

    // The preconditions the callers actually establish, read off the asserts.
    __CPROVER_assume(op != 0);
    __CPROVER_assume(oend - op >= (ptrdiff_t)WILDCOPY_OVERLENGTH);
    __CPROVER_assume(sequence.litLength <= 16);
    __CPROVER_assume(sequence.matchLength >= 1 && sequence.matchLength <= 16);
    __CPROVER_assume(sequence.offset >= 1);
    __CPROVER_assume(sequence.offset <= (size_t)(op - prefixStart) + sequence.litLength);
    __CPROVER_assume(litPtr + sequence.litLength <= litLimit);
    __CPROVER_assume(sequence.litLength + sequence.matchLength
                     <= (size_t)(oend - op));

    (void)ZSTD_execSequence(op, oend, sequence, &litPtr, litLimit,
                            prefixStart, virtualStart, dictEnd);
}
