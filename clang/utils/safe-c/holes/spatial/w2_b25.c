#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct F{int n;int d[__counted_by(n)];};struct G{struct F f;};int main(void){struct G*g=malloc(sizeof*g+16);if(!g)return 1;g->f.n=100;struct F*__single s=&g->f;printf("%d",s->d[90]);free(g);}
