// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;}; struct o{struct s s;};
static struct o get(struct s*x){ return (struct o){*x}; }
int main(int c,char**v){ struct s x={0}; x.p=malloc(4); struct o y=get(&x); free(_Take(y.s.p)); if(x.p) *x.p=3; }
