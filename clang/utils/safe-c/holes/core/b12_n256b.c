#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct n { struct n *_Owned _Nullable k; struct n *_Owned _Nullable m; int v; } N;
typedef struct arr { size_t cap; struct n *_Owned _Nullable *_Owned _Nullable __counted_by(cap) items; } A;
N *_Owned _Nullable mk(int v) { N *_Owned _Nullable r = calloc(1, sizeof *r); if (!r) return NULL; r->v = v; return r; }
void del(N *_Owned _Nullable p) { if (!p) return; del(_Take(p->k)); del(_Take(p->m)); free(p); }
N *_Nullable kid(const N *p) { return p->k; }
void chop(N *a) { del(_Take(a->k)); }
_Bool put(N *p, N *_Taken_if_true x) { if (p->v < 0) return 0; del(_Take(p->k)); p->k = x; return 1; }
int put0(N *p, N *_Taken_if_zero x) { if (p->v < 0) return 1; del(_Take(p->k)); p->k = x; return 0; }
void grow(N *p) { N *_Owned _Nullable x = mk(7); if (!x) return; del(_Take(p->k)); p->k = x; }
volatile int sink;
#define NEW(p) N *_Owned _Nullable p = mk(1); if (!p) return 1;
#define TREE(p) N *_Owned _Nullable p = mk(1); if (!p) return 1; p->k = mk(2); p->m = mk(3);
#define U(q) if (q) sink = (q)->v;
long f(N *p, N *_Taken_if_true x) { if (p->v < 0) return 0; p->k = x; return 0x10000; }
int main(void) { NEW(p) NEW(x) unsigned short r = f(p, x); if (!r) del(x); del(p); return 0; }
