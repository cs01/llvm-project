// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;};
static struct s mk(void){ struct s x={0}; x.p=malloc(4); return x; }
static void f(struct s a){ free(_Take(a.p)); }
int main(int c,char**v){ struct s x={0}; f(x=mk()); free(_Take(x.p)); }
