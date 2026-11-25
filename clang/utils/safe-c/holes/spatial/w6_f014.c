#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct O;struct Q{int a[2];};NI int g(struct O*o){struct Q*q=(struct Q*)o;return q->a[0];}struct O{char c;};int main(void){struct O o={1};return g(&o);}
