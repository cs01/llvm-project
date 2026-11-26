#include <stdlib.h>

int main(int argc, char **argv) {
  int *_Owned _Nullable p = malloc(4 * sizeof(int));
  if (!p) return 1;
  if (argc > 1)
    return 2;             // p is never freed on this path
  free(p);
  return 0;
}
