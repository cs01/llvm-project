// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;}; struct o{int k; struct s s;};
int main(int c,char**v){ struct s x={0}; x.p=malloc(4); struct o y={.s=x}; free(_Take(y.s.p)); if(x.p) *x.p=3; }
