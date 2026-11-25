#include <stdio.h>
#include <stdlib.h>
/* Codegen computes a __bidi_indexable destination's address before the right
   side; ClangIR (what is checked) evaluates the right side first. */
struct N { int *_Owned _Nullable __bidi_indexable f; };
struct V { long a[3]; };
struct W { struct N *_Owned _Nullable k; struct V *_Owned _Nullable v; };
static int *_Owned _Nullable g(struct W *w) {
  free(_Take(w->k));
  w->v = calloc(1, sizeof(struct V)); /* takes the freed slot */
  w->k = calloc(1, sizeof(struct N));
  return calloc(1, 4);
}
int main() {
  struct W w = {0};
  w.k = calloc(1, sizeof(struct N));
  if (!w.k) return 1;
  w.k->f = g(&w); /* writes 24 bytes to the freed old w.k */
  if (w.v) printf("victim %lx (must be 0)\n", w.v->a[0]);
}
