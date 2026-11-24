#include <stdlib.h>
#include <safec.h>
struct opaque { int v; };
struct shared { int a; long b; };
void frees(int *_Owned p) { free(p); }
void counts(int *__counted_by(n) p, int n) { if (n) p[n - 1] = 1; }
int look(struct shared *s) { return s->a; }
int agree(struct opaque *o, int *__counted_by(n) p, int n, struct shared *s) { return o->v + (n ? p[0] : 0) + s->a; }
