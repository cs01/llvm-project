// Reachability: can a stream drive BIT_initDStream with a buffer that ends
// where the stream ends and is shorter than sizeof(size_t)?
//
// This is the open question in FINDING-initdstream-limitptr.md. That finding
// proves BIT_initDStream forms `start + 8` on a buffer of 1..7 bytes; whether
// that pointer leaves the underlying *object* depends on the caller, because a
// short stream sitting inside a larger frame buffer lands harmlessly in the
// bytes after it.
//
// The four-stream Huffman path is the candidate, and the shape is forced rather
// than lucky: stream 4 starts at istart3 + length3 and runs to the end of cSrc,
// so istart4 + length4 IS the end of the allocation. length1..3 are 16-bit
// fields read straight out of the stream, and the only guards before the calls
// are cSrcSize >= 10, dstSize >= 6, length4 <= cSrcSize and opStart4 <= oend.
// None of them bounds a stream length below.
//
// cSrc is allocated at exactly cSrcSize bytes, which is what a caller decoding
// into a caller-owned input buffer actually has. A counterexample here is a
// real trace: bounded unwinding under-approximates, so a failure it finds is
// genuine even though the absence of one would prove nothing.
#include "huf_decompress.c"

size_t nondet_size(void);
U32 nondet_u32(void);

#define DST_CAP 16u
#define DT_LOG   6u

void harness(void)
{
    size_t const cSrcSize = nondet_size();
    // >= 10 is the function's own minimum; the upper bound only keeps the
    // symbolic state small enough to solve.
    __CPROVER_assume(cSrcSize >= 10 && cSrcSize <= 32);

    // Exactly cSrcSize bytes, so a pointer past the end of the stream is a
    // pointer past the end of the object rather than into slack.
    void *const cSrc = __CPROVER_allocate(cSrcSize, 0);

    static BYTE dst[DST_CAP];

    // The table is not what is under test: the out-of-object pointer is formed
    // in BIT_initDStream before any table entry is read. Give it a well-formed
    // descriptor so the call is not rejected for an unrelated reason.
    static HUF_DTable DTable[1 + (1 << DT_LOG)];
    DTableDesc dtd;
    dtd.tableLog = (BYTE)DT_LOG;
    dtd.tableType = 0;
    dtd.maxTableLog = (BYTE)DT_LOG;
    dtd.reserved = 0;
    ZSTD_memcpy(DTable, &dtd, sizeof(dtd));

    (void)HUF_decompress4X1_usingDTable_internal_body(dst, DST_CAP, cSrc,
                                                     cSrcSize, DTable);
}
