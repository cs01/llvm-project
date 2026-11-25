#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct O; NI struct O*g(int*p){return (struct O*)p;} struct O{int a[100];}; int main(void){int x=1;struct O*o=g(&x);o->a[3]=7;return 0;}
