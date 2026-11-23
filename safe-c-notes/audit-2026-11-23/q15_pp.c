#include <stdlib.h>
#include <stdio.h>
static void destroy(int *_Nullable *pp) { free(*pp); }
int main(void) {
  int *_Nullable p = malloc(sizeof(int)); if (!p) return 1;
  destroy(&p);
  int *_Nullable s = malloc(sizeof(int)); if (!s) return 1; *s = 0x51515151;
  printf("%x\n", *p);
  return 0;
}
