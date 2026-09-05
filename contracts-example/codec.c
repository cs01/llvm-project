#include "codec.h"

// The definition inherits the header's contracts. A call site finds them by
// walking the redeclaration chain, so the header is what a caller is checked
// against.
int *codec_alloc(unsigned long n) { return (int *)0; }

unsigned long codec_decompress(void *dst, unsigned long dstCap,
                               const void *src, unsigned long srcSize) {
  // Bodies mutate their own parameter copies, which is exactly why 'post' has
  // to say old(dstCap) rather than dstCap.
  dstCap -= 1;
  return dstCap;
}
