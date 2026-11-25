// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
int main(int c,char**v){ int*p=calloc(2,4); if(!p) return 1; printf("%f\n",frexp(3.0*c,&p[c+3])); }
