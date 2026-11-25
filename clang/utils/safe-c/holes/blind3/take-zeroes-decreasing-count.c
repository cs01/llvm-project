#include <stdlib.h>
struct h { unsigned n; int *_Owned _Nullable __counted_by(n) f; int *_Owned _Nullable __counted_by(4 - n) g; };
int main(void) {
  struct h h = {0};
  int *_Owned _Nullable a = calloc(1, 4);
  int *_Owned _Nullable b = calloc(3, 4);
  h.f = a; h.g = b; h.n = 1;
  free(_Take(h.f));
  if (h.g) h.g[3] = 1;
  return 0;
}
