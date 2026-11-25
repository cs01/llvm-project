#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct V{int n;int*__counted_by(n) d;};static int big[50];NI int grow(struct V*p){p->d=big;p->n=50;return 40;}NI int shrink(struct V*p){static int z[1];p->d=z;p->n=1;return 3;}
int main(void){struct{int a[4];int k;}q={{1,2,3,4},77};struct V v={4,q.a};struct V*p=&v;(void)p;volatile int k=5;struct {struct V z;} w={.z=v,.z.n=k};w.z.d[4]=1; return q.k!=77?42:0;}
