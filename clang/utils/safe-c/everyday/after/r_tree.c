#include <stdio.h>
#include <stdlib.h>
struct t{int k; struct t*_Owned _Nullable l,*_Owned _Nullable r;};
static void ins(struct t*root,int k){ for(;;){ struct t*_Nullable d=k<root->k?root->l:root->r; if(d){ root=d; continue; } struct t*_Owned _Nullable n=calloc(1,sizeof*n); if(!n) return; n->k=k; if(k<root->k) root->l=n; else root->r=n; return; } }
int main(void){ struct t*_Owned _Nullable r=calloc(1,sizeof*r); if(!r) return 1; r->k=3; ins(r,1); printf("%d\n",r->k); }
