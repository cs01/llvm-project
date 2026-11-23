#include <stdlib.h>
#include <stdio.h>
__attribute__((noinline)) static void clobber(void) { volatile int a[256]; for (int i = 0; i < 256; i++) a[i] = 0x57575757; }
int main(void) {
  int *_Nullable arr[1] = {0};
  for (int i = 0; i < 1; i++) { int x[64] = {0}; x[5] = 1; arr[0] = &x[5]; }
  { volatile int y[64]; for (int i = 0; i < 64; i++) y[i] = 0x58585858; }
  int *_Nullable q = arr[0];
  if (q) printf("scope: %x\n", *q);
  int *_Nullable p = malloc(4); if (!p) return 0;
  int **_Nullable h = malloc(sizeof(int *)); if (!h) return 0;
  *h = p; free(p);
  int *_Nullable s = malloc(4); if (!s) return 1; *s = 0x59595959;
  printf("heap slot: %x\n", **h);
  return 0;
}
