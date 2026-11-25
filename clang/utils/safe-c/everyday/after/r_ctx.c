#include <stdio.h>
struct counter{int n;}; typedef void(*cb)(void*ctx,int v);
static void each(const int*__counted_by(n) a,int n,cb f,void*ctx){ for(int i=0;i<n;i++) f(ctx,a[i]); }
static void add(void*ctx,int v){ struct counter*__single c; __unsafe { c=ctx; } c->n+=v; }
int main(void){ int a[3]={1,2,3}; struct counter c={0}; each(a,3,add,&c); printf("%d\n",c.n); }
