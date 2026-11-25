#include <stdio.h>
#include <stdlib.h>
int main(void) {
  int *_Owned _Nullable p = malloc(8);
  if (!p) return 0;
  volatile int i = 2;
  sscanf("7", "%d", &p[i]);
  free(p);
  return 0;
}
