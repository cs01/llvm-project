// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ int*p=calloc(2,4); if(!p) return 1; printf("%f\n",__builtin_frexp(3.0*c,&p[c+3])); double*d=calloc(1,8); if(!d) return 1; __builtin_modf(3.4*c,d+c+2); }
