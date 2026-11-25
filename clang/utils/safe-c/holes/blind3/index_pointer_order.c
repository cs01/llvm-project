#include <stdio.h>
#include <stdlib.h>
/* i[p]: codegen loads p before evaluating i; ClangIR (what is checked) the other way. */
struct G { int a[4]; };
struct V { int a[4]; };
struct X { struct G *_Owned _Nullable s; struct V *_Owned _Nullable v; };
static int rx(struct X *x) {
  free(_Take(x->s));
  x->v = calloc(1, sizeof(struct V)); /* takes the freed slot */
  x->s = calloc(1, sizeof(struct G));
  return 0;
}
int main() {
  struct X x = {0};
  x.s = calloc(1, sizeof(struct G));
  if (!x.s) return 1;
  rx(&x)[x.s->a] = 41; /* writes to the freed old x.s */
  if (x.v) printf("victim %d (must be 0)\n", x.v->a[0]);
}
