// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct n{int*_Owned _Nullable l;};
int main(int c,char**v){ struct n*a=calloc(1,8),*b=calloc(1,8); if(!a||!b) return 1; int*k=malloc(4); if(!k) return 1; a->l=k; b->l=k; free(_Take(a->l)); free(_Take(b->l)); }
