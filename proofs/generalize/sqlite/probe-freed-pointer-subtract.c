// Minimal model of storeRawNames' realloc fixup: after realloc succeeds, the
// old pointer value is still read (compared, and used as the left operand of a
// pointer subtraction). Does CBMC see that?
#include <stdlib.h>
unsigned nondet_uint(void);
void harness(void) {
  unsigned n = nondet_uint();
  __CPROVER_assume(n >= 1 && n <= 4);
  char *buf = malloc(n);
  __CPROVER_assume(buf != 0);
  char *localPart = buf;              // "always points into tag->buf"
  char *temp = realloc(buf, n + 8);
  __CPROVER_assume(temp != 0);
  // buf is now indeterminate if realloc moved the block.
  long off = localPart - buf;         // subtraction using the freed value
  localPart = temp + off;
  __CPROVER_assert(off >= 0, "offset nonneg");
  (void)localPart;
  free(temp);
}
