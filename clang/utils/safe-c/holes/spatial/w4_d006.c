#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct F{int n;int d[__counted_by(n)];}; NI int r(struct F*__single f){return f->d[6];} int main(void){int*a=malloc(4);if(!a)return 1;*a=100;int*__single p=a;printf("%d",r((struct F*)p));}
