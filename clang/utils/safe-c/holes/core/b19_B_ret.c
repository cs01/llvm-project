#include <stdlib.h>
typedef struct n { struct n *_Owned _Nullable k; int v; } N;
char put(N *p, N *_Taken_if_true x) { int r = 256; p->k = x; return r; }
int main(void) { N *_Owned _Nullable p = calloc(1, sizeof *p), *_Owned _Nullable x = calloc(1, sizeof *x); if (!p || !x) return 0;
  if (!put(p, x)) free(x);
  return p->k ? p->k->v : 1; }
