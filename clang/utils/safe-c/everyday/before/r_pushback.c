// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct vec{int*_Owned _Nullable __counted_by(cap) d; int len,cap;};
static bool push(struct vec*_Exclusive v,int x){ if(v->len==v->cap){ int nc=v->cap?v->cap*2:4; int*_Nullable nd=realloc(v->d,nc*sizeof(int)); if(!nd) return false; v->d=nd; v->cap=nc; } v->d[v->len++]=x; return true; }
int main(void){ struct vec v={0}; for(int i=0;i<9;i++) push(&v,i); printf("%d\n",v.len); }
