#include <stdlib.h>
typedef struct n { struct n *_Owned _Nullable k; int v; } N;
_Bool put(N *p, N *_Taken_if_true x) { p->k = x; return 1; }
int main(void) { N *_Owned _Nullable p = calloc(1, sizeof *p), *_Owned _Nullable x = calloc(1, sizeof *x); if (!p || !x) return 0;
  int r = put(p, x); int *z = &r; *z = 0;
  if (!r) free(x);
  return p->k ? p->k->v : 1; }
