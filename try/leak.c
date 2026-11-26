#include <stdlib.h>

int main(int argc, char **argv) {
  int *_Owned _Nullable p = malloc(4 * sizeof(int));
  if (!p) return 1;
  if (argc > 1)
    return 2;             // p is never freed on this path
    // error: 'p' still owns memory when the function returns; free it first, or give it away
  free(p);
  return 0;
}
