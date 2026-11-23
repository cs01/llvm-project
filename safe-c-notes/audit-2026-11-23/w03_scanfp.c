#include <stdlib.h>
#include <stdio.h>
int main(void) {
  void *__single _Nullable w = 0;
  sscanf("0x41414141", "%p", &w);
  void *__single _Nullable t = &&L;
  sscanf("0x41414140", "%p", &t);
  if (!t) return 1;
  goto *t;
L:
  free(w);
  return 0;
}
