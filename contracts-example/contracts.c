#include "contracts.h"

// Contracts are declared once, on the prototype, and inherited by the
// definition. A call site finds them by walking the redeclaration chain, so
// the header is what callers are checked against.
int *allocate(unsigned long n) { return (int *)0; }

unsigned long decompress(void *dst, unsigned long dstCap,
                         const void *src, unsigned long srcSize) {
  // A body may mutate its own parameter copies. That is exactly why the
  // postcondition has to say old(dstCap) and not dstCap.
  dstCap -= 1;
  return dstCap;
}

void put(int *buf, unsigned long len, unsigned long i, int v) { buf[i] = v; }
