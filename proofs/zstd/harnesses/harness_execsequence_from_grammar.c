// Same proof as harnesses/harness_execsequence.c, with the seven preconditions
// deleted from the harness: they are now pre clauses on ZSTD_execSequence
// itself, and --enforce-contract is what supplies them.
#include "lib/decompress/zstd_decompress_block.c"

#define DST_CAP   48u
#define LIT_CAP   16u

unsigned long nondet_ulong(void);

void harness(void)
{
    static BYTE dst[DST_CAP];
    static BYTE lit[LIT_CAP];

    BYTE *const ostart = dst;
    BYTE *const oend   = dst + DST_CAP;

    size_t const opOffset = nondet_ulong();
    __CPROVER_assume(opOffset <= DST_CAP);
    BYTE *op = ostart + opOffset;

    const BYTE *litPtr = lit;
    const BYTE *const litLimit = lit + LIT_CAP;

    seq_t sequence;
    sequence.litLength   = nondet_ulong();
    sequence.matchLength = nondet_ulong();
    sequence.offset      = nondet_ulong();

    const BYTE *const prefixStart  = ostart;
    const BYTE *const virtualStart = ostart;
    const BYTE *const dictEnd      = ostart;

    // Only the harness's own domain bounds remain. Everything the FUNCTION
    // requires is now stated on the function.
    __CPROVER_assume(sequence.litLength <= LIT_CAP);
    __CPROVER_assume(sequence.matchLength <= 16);

    (void)ZSTD_execSequence(op, oend, sequence, &litPtr, litLimit,
                            prefixStart, virtualStart, dictEnd);
}
