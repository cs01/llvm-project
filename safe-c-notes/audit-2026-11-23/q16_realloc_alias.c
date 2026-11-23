#include <stdlib.h>
#include <stdio.h>
int main(void) {
  int *_Nullable p = malloc(4 * sizeof(int)); if (!p) return 1;
  int *q = p + 1;
  int *_Nullable n = realloc(p, 1 << 22); if (!n) return 1;
  *q = 0x52525252;
  printf("%x\n", *q);
  return 0;
}
