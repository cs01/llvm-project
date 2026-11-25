// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef float f2 __attribute__((ext_vector_type(4)));
int main(int c,char**v){ f2 a[2]={0}; f2*p=a; p[c+1].w=1; printf("%f\n",p[c].x); }
