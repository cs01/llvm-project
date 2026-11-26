#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
struct vec{int*_Owned _Nullable __counted_by(cap) d; int len,cap;};
static bool push(struct vec*v,int x){ if(v->len==v->cap){ int nc=v->cap?v->cap*2:4; int*_Owned _Nullable nd=realloc(_Take(v->d),(size_t)nc*sizeof(int)); if(!nd) return false; v->d=nd; v->cap=nc; } if(!v->d) return false; v->d[v->len++]=x; return true; }
int main(void){ struct vec v={0}; for(int i=0;i<9;i++) push(&v,i); printf("%d\n",v.len); free(_Take(v.d)); }
