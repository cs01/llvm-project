// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct n{int*_Owned _Nullable l; int v;};
static void clr(struct n*_Exclusive r){ free(_Take(r->l)); }
int main(void){ struct n a={0}; int*k=malloc(4); if(!k) return 1; a.l=k; clr(&a); *k=5; }
