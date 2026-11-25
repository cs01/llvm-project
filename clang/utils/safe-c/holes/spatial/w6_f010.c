#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
union O;NI int g(union O*);int main(void){int x=1;return g((union O*)&x);} union O{int a[100];}; NI int g(union O*o){return o->a[3];}
