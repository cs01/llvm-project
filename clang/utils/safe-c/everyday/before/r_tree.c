// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct t{int k; struct t*_Owned _Nullable l,*_Owned _Nullable r;};
static void ins(struct t*_Owned _Nullable*_Exclusive root,int k){ while(*root) root=k<(*root)->k?&(*root)->l:&(*root)->r; struct t*n=calloc(1,sizeof*n); if(!n) return; n->k=k; *root=n; }
int main(void){ struct t*_Owned _Nullable r=NULL; ins(&r,3); ins(&r,1); printf("%d\n",r?r->k:0); }
