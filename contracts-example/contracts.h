// Contracts as they are meant to be written through the portable header.
#ifndef CONTRACTS_EXAMPLE_H
#define CONTRACTS_EXAMPLE_H

#include <c_contracts.h>

int is_error(int r) __attribute__((pure));

unsigned long decompress(void *dst, unsigned long dstCap,
                         const void *src, unsigned long srcSize)
  c_writes  (dst, dstCap)
  c_reads   (src, srcSize)
  c_pre     (dstCap > 0)
  c_returns (c_result <= c_old(dstCap) || is_error((int)c_result));

int *allocate(unsigned long n)
  c_pre     (n > 0)
  c_returns (c_result != 0);

void put(int *buf, unsigned long len, unsigned long i, int v)
  c_pre    (buf != 0)
  c_pre    (i < len)
  c_assigns(buf[i]);

void buf_free_if_needed(void);

#endif
