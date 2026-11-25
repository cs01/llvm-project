// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{const int*_Static _Nullable __counted_by(n) p; int n;};
static const int K[2]={1,2};
int main(int c,char**v){ struct s x={K,2}; if(__builtin_add_overflow(c,2,&x.n)) return 1; printf("%d\n",x.p?x.p[2]:0); }
