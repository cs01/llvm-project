// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static int f(int*_Nullable p){ return __builtin_mul_overflow(4,1,p); }
int main(int c,char**v){ printf("%d\n",f(NULL)); }
