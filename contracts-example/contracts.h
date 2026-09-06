// Contracts as they are meant to be written. No macros, no prefixes, no
// annotations pretending to be comments. This is the grammar.
#ifndef CONTRACTS_EXAMPLE_H
#define CONTRACTS_EXAMPLE_H

int is_error(int r) __attribute__((pure));

unsigned long decompress(void *dst, unsigned long dstCap,
                         const void *src, unsigned long srcSize)
  pre  (dst != 0)
  pre  (src != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap) || is_error((int)r));

int *allocate(unsigned long n)
  pre  (n > 0)
  post (r: r != 0);

void put(int *buf, unsigned long len, unsigned long i, int v)
  pre (buf != 0)
  pre (i < len);

void buf_free_if_needed(void);

#endif
