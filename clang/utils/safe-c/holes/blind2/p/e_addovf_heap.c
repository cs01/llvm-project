// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ int*p=calloc(2,4); if(!p) return 1; printf("%d\n",__builtin_mul_overflow(c,1,&p[c+3])); }
