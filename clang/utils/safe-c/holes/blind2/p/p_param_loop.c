// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct n{int*_Owned _Nullable l;};
static void put(struct n*__counted_by(n) _Exclusive a,int n,int*_Owned k){ for(int i=0;i<n;i++) a[i].l=k; }
int main(int c,char**v){ struct n a[2]={0}; int*k=malloc(4); if(!k) return 1; put(a,2,k); free(_Take(a[0].l)); free(_Take(a[1].l)); }
