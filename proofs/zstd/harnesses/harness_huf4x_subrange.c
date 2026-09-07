// Does the caller chain actually put a short Huffman stream at the end of the
// caller's own object? This is the question left open by
// FINDING-initdstream-limitptr.md.
//
// harness_huf4x_stream4.c gave cSrc its own allocation, which proved the four-
// stream path's guards permit a stream shorter than sizeof(size_t) but not that
// a real caller ever produces one at an object boundary. In the library cSrc is
// a sub-range: ZSTD_decodeLiteralsBlock passes `istart + lhSize` with size
// `litCSize`, and its guard is
//
//     RETURN_ERROR_IF(litCSize + lhSize > srcSize, corruption_detected, "");
//
// which is strictly greater, so `lhSize + litCSize == srcSize` is accepted --
// the literals section may end exactly where the caller's buffer ends. In
// ZSTD_decompressBlock(), a public entry point, that buffer is the user's own.
//
// So this harness allocates the whole block and hands the decoder the tail of
// it, which is the shape the library actually produces.
#include "huf_decompress.c"

size_t nondet_size(void);

#define DST_CAP 16u
#define DT_LOG   6u

void harness(void)
{
    size_t const total  = nondet_size();   // the caller's whole buffer
    size_t const lhSize = nondet_size();   // the literals header before it
    __CPROVER_assume(total >= 12 && total <= 34);
    __CPROVER_assume(lhSize >= 1 && lhSize <= 2);
    __CPROVER_assume(total - lhSize >= 10); // the function's own minimum

    unsigned char *const block = __CPROVER_allocate(total, 0);

    // Exactly what ZSTD_decodeLiteralsBlock passes when the literals run to the
    // end of the block: cSrc + cSrcSize == block + total == end of the object.
    const void *const cSrc = block + lhSize;
    size_t const cSrcSize = total - lhSize;

    static BYTE dst[DST_CAP];
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
