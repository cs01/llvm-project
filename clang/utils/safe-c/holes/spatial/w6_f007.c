#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
struct O; typedef struct O O;NI int g(O*);int main(void){char x[4]={1};return g((O*)x);} struct O{char a[100];}; NI int g(struct O*o){return o->a[5];}
