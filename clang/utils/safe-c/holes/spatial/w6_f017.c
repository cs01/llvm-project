#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct O;NI int g(struct O*o){int*q=(int*)o;return *q;}struct O{char c;};int main(void){struct O o={1};return g(&o);}
