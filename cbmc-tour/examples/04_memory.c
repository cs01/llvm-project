#include <stdlib.h>
#include <string.h>

char *dup_prefix(const char *src, size_t n) {
  char *dst = malloc(n);          // no +1 for the NUL terminator
  memcpy(dst, src, n);
  dst[n] = '\0';                  // out-of-bounds write
  return dst;
}

int main(void) {
  size_t n = nondet_size_t();
  __CPROVER_assume(n > 0 && n <= 4);
  char buf[8];
  char *p = dup_prefix(buf, n);
  free(p);
  return 0;
}
