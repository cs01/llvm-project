#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct O; NI struct O*g(int*p){return (struct O*)p;} struct O{int*_Nullable a;}; int main(void){int x[2]={0x41414141};struct O*o=g(x);if(o->a)return *o->a;return 0;}
