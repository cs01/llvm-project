#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct a{int n;int d[__counted_by(n)];};
static int f(struct a*p){int k=100;memcpy(&p->n,&k,4);return p->d[20];}
int main(void){struct a*p=malloc(8);if(!p)return 1;p->n=1;return f(p);}
