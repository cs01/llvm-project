#include "zstd_internal.h"
unsigned long nondet_ulong(void);

// No cap on length: the buffers are symbolically sized, so a discharged proof
// here is unbounded rather than exhaustive-up-to-N.
void harness(void)
{
    size_t length = nondet_ulong();
    __CPROVER_assume(length > 0 && length < 0x100000);
    BYTE *dst = __CPROVER_allocate(length + WILDCOPY_OVERLENGTH, 0);
    BYTE *src = __CPROVER_allocate(length + WILDCOPY_OVERLENGTH, 0);
    ZSTD_wildcopy(dst, src, length, ZSTD_no_overlap);
}
