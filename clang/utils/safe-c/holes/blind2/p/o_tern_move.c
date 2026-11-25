// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;};
int main(int c,char**v){ struct s a={0},b={0}; int*t=malloc(4); if(!t) return 1; (c?&a:&b)->p=t; free(t); if(a.p) free(_Take(a.p)); }
