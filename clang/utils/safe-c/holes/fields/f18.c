#include <stdlib.h>
struct box { int *_Owned _Nullable p; };
struct two { struct box a, b; };
struct nd { struct box a; struct nd *_Owned _Nullable next; };
static void clr(struct box *b) { free(_Take(b->p)); }
static void put(struct box *b, int v) { int *_Owned _Nullable n = malloc(sizeof(int)); if (n) *n = v; clr(b); b->p = n; }
static int *_Nullable get(struct box *b) { return b->p; }
static void both(struct two *t) { clr(&t->a); clr(&t->b); }
static int *_Nullable any2(struct two *t, int k) { if (k) return get(&t->b); return get(&t->a); }
static void free_nd(struct nd *_Owned _Nullable n) { if (!n) return; clr(&n->a); free_nd(_Take(n->next)); free(n); }
struct out { struct two t; struct box c; };
int main(void){ struct out o={0}; put(&o.t.a,1); int*_Nullable p=get(&o.t.a); both(&o.t); return p?*p:0; }
