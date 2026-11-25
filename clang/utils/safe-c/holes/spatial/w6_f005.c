#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct O;NI long g(struct O*);int main(void){int x=1;return g((struct O*)&x);} struct O{long a;}; NI long g(struct O*o){return o->a;}
