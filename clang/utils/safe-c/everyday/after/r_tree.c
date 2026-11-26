#include <stdio.h>
#include <stdlib.h>
struct t{int k; struct t*_Owned _Nullable l,*_Owned _Nullable r;};
static void ins(struct t*root,int k){ for(;;){ if(k<root->k){ if(!root->l){ struct t*_Owned _Nullable n=calloc(1,sizeof*n); if(!n) return; n->k=k; root->l=n; return; } struct t*_Nullable c=root->l; if(!c) return; root=c; continue; } if(!root->r){ struct t*_Owned _Nullable n=calloc(1,sizeof*n); if(!n) return; n->k=k; root->r=n; return; } struct t*_Nullable c=root->r; if(!c) return; root=c; } }
static void drop(struct t*_Owned _Nullable x){ if(!x) return; drop(_Take(x->l)); drop(_Take(x->r)); free(x); }
int main(void){ struct t*_Owned _Nullable r=calloc(1,sizeof*r); if(!r) return 1; r->k=3; ins(r,1); printf("%d\n",r->k); drop(r); }
