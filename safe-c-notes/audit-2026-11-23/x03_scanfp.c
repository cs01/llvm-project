#include <stdlib.h>
#include <stdio.h>
int main(void) {
  void *__single _Nullable w = 0;
  sscanf("0x41414141", "%p", &w);
  printf("freeing %p\n", w);
  free(w);
  return 0;
}
