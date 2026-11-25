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
static int two3(struct box*x,struct nd*_Nullable y){ int*_Nullable p=get(x); if(y) clr(&y->a); return p?*p:0; }
static int run(struct nd*n){ struct nd*_Nullable d=n->next; if(!d) return 0; return two3(&d->a,d); }
int main(void){ struct nd n={0}; n.next=calloc(1,sizeof(struct nd)); if(!n.next) return 1; put(&n.next->a,1); return run(&n); }
