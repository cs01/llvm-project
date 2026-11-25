// opts: +mf2_enum2 std=c23
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
enum e:char{A,B}; void f(enum e*x); int main(int c,char**v){ enum e*x=malloc(1); if(!x) return 1; *x=A; f(x); printf("%d\n",*x); free(x); }
