#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef float f4 __attribute__((ext_vector_type(4)));
int main(int c,char**v){f4*p=calloc(1,16);if(!p)return 1;return (int)p[c+5].x;}
