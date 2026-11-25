// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;};
int main(int c,char**v){ struct s x={0}; x.p=malloc(4); struct s y[2]={x,x}; free(_Take(y[0].p)); free(_Take(y[1].p)); }
