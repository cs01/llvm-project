#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct F{int n;int d[__counted_by(n)];}; struct G{int a;struct F f;}; int main(void){struct G*g=calloc(1,sizeof*g+8);if(!g)return 1;g->f.n=100;volatile int i=90;printf("%d",g->f.d[i]);}
