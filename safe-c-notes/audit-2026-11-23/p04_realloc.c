#include <stdlib.h>
#include <stdio.h>
struct V { int *_Nullable __counted_by_or_null(n) d; size_t n; };
static void grow(struct V *v) {
  int *_Nullable nd = realloc(v->d, 4096 * 64 * sizeof(int));
  if (!nd) return;
  v->d = nd; v->n = 4096*64;
}
int main(void) {
  struct V v = {0};
  int *_Nullable d = malloc(4 * sizeof(int));
  if (!d) return 1;
  v.d = d; v.n = 4;
  if (!v.d) return 1;
  int *first = &v.d[0];
  *first = 9;
  grow(&v);
  *first = 0x44444444;
  printf("%x\n", *first);
  return 0;
}
