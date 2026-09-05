// Functional correctness of the LZ reconstruction.
//
// The bounds proofs say ZSTD_execSequence does not write outside the buffer.
// They say nothing about whether it writes the *right bytes*. This harness
// states the LZ semantics directly and asks CBMC to prove the implementation
// agrees with them:
//
//   out[i]              == literals[i]                for i < litLength
//   out[litLength + j]  == out[litLength + j - offset] for j < matchLength
//
// The second is deliberately self-referential: a match may overlap its own
// output, which is the whole point of LZ77 and the reason ZSTD_overlapCopy8
// exists.
#include "zstd_decompress_block.c"

#define DST_CAP   48u
#define LIT_CAP   16u

unsigned int  nondet_uint(void);
unsigned long nondet_ulong(void);

void harness(void)
{
    static BYTE dst[DST_CAP];
    static BYTE lit[LIT_CAP + WILDCOPY_OVERLENGTH];

    BYTE *const ostart = dst;
    BYTE *const oend   = dst + DST_CAP;
    BYTE *op = ostart;

    const BYTE *litPtr = lit;
    const BYTE *const litLimit = lit + LIT_CAP;

    seq_t sequence;
    sequence.litLength   = nondet_ulong();
    sequence.matchLength = nondet_ulong();
    sequence.offset      = nondet_ulong();

    const BYTE *const prefixStart  = ostart;
    const BYTE *const virtualStart = ostart;
    const BYTE *const dictEnd      = ostart;

    __CPROVER_assume(op != 0);
    __CPROVER_assume(oend - op >= (ptrdiff_t)WILDCOPY_OVERLENGTH);
    __CPROVER_assume(sequence.litLength <= 16);
    __CPROVER_assume(sequence.matchLength >= 3 && sequence.matchLength <= 16);
    __CPROVER_assume(sequence.offset >= 1);
    __CPROVER_assume(sequence.offset <= (size_t)(op - prefixStart) + sequence.litLength);
    __CPROVER_assume(litPtr + sequence.litLength <= litLimit);
    __CPROVER_assume(sequence.litLength + sequence.matchLength
                     <= (size_t)(oend - op));

    size_t const litLength   = sequence.litLength;
    size_t const matchLength = sequence.matchLength;
    size_t const offset      = sequence.offset;

    (void)ZSTD_execSequence(op, oend, sequence, &litPtr, litLimit,
                            prefixStart, virtualStart, dictEnd);

    // Literals were copied verbatim.
    __CPROVER_assert(
        __CPROVER_forall { unsigned i; i < litLength ==> dst[i] == lit[i] },
        "literals copied verbatim");

    // Match bytes equal the bytes they reference, including when the match
    // overlaps its own output.
    __CPROVER_assert(
        __CPROVER_forall { unsigned j;
            j < matchLength ==> dst[litLength + j] == dst[litLength + j - offset] },
        "match bytes equal the referenced bytes");
}
