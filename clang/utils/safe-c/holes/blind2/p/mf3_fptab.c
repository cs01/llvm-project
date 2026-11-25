// opts: +mf3_fptab2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct a{int x;}; struct o{int(*f)(struct a*);}; extern const struct o O; int main(void){ struct a*a=calloc(1,4); if(!a) return 1; printf("%d\n",O.f(a)); free(a);}
