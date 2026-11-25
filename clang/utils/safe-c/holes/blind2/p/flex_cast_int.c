// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct a{int n; int d[__counted_by(n)];};
static int f(struct a*p){ int*q=(int*)p; *q=100; return p->d[20]; }
int main(int c,char**v){ struct a*p=malloc(8); if(!p) return 1; p->n=1; printf("%d\n",f(p)); }
