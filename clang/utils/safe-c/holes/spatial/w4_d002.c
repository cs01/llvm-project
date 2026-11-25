#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct F{int n;int d[__counted_by(n)];}; NI int r(struct F*__single f){return f->d[6];} struct A{int k;};int main(void){struct A a={100};printf("%d",r((struct F*__single)&a));}
