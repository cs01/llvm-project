#include <stdlib.h>
#include <stdio.h>
int *_Nullable g;
static void stash(int *p) { g = p; }
int main(void) {
  int *_Nullable p = malloc(sizeof(int));
  if (!p) return 1;
  *p = 5;
  stash(p);
  free(p);
  long *_Nullable s = malloc(sizeof(int));
  if (!s) return 1;
  *s = 0x42424242;
  if (g) printf("%x\n", *g);
  return 0;
}
