#include <stdlib.h>
#include <stdio.h>
static void freep(int *_Nullable *pp) { free(*pp); }
int *_Nullable g;
int main(void) {
  { int *__single _Nullable p __attribute__((cleanup(freep))) = malloc(sizeof(int)); g = p; }
  int *_Nullable s = malloc(sizeof(int)); if (!s) return 1; *s = 0x55555555;
  if (g) printf("%x\n", *g);
  return 0;
}
