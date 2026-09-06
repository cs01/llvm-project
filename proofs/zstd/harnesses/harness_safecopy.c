#include "zstd_decompress_block.c"
unsigned long nondet_ulong(void);

// No cap tied to a fixed buffer: both regions are symbolically allocated, so a
// discharged proof is unbounded in length.
void harness(void)
{
    size_t length = nondet_ulong();
    __CPROVER_assume(length > 0 && length < 0x40000000);
    BYTE *op = __CPROVER_allocate(length + WILDCOPY_OVERLENGTH, 0);
    BYTE *ip = __CPROVER_allocate(length + WILDCOPY_OVERLENGTH, 0);
    const BYTE *oend_w = op + length;   // no wildcopy slack: forces the tail loop
    ZSTD_safecopy(op, oend_w, ip, length, ZSTD_no_overlap);
}
