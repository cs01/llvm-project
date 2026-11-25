#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct F{int n;int d[__counted_by(n)];}; NI int r(struct F*__single f){return f->d[6];} int main(void){struct F*f=calloc(1,12);if(!f)return 1;struct F*__single s=f;*(unsigned*)&f->n=400;printf("%d",r(s));}
