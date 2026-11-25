#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int c,char**v){int*p=calloc(2,4);if(!p)return 1;return __builtin_mul_overflow(c,1,&p[c+3]);}
