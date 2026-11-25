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
static int run(struct nd*n){ struct box*_Nullable a=NULL; for(struct nd*_Nullable c=n;c;c=c->next){ if(a){ if(c->next) free_nd(_Take(c->next)); } a=&c->a; } return 0; }
static int r2(struct nd*n){ struct box*_Nullable a=NULL; struct nd*_Nullable c=n; for(int i=0;i<3;i++){ if(!c) break; if(i==2){ free_nd(_Take(n->next)); if(a){ int*_Nullable p=get(a); return p?*p:0; } } a=&c->a; if(c->next) c=c->next; } return 0; }
int main(void){ struct nd n={0}; n.next=calloc(1,sizeof(struct nd)); if(!n.next) return 1; put(&n.next->a,1); return r2(&n)+run(&n); }
