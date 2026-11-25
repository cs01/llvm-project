// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct n{int*_Owned _Nullable l;};
int main(int c,char**v){ struct n a[4]={0}; int*k=malloc(4); if(!k) return 1; a[c].l=k; free(k); free(_Take(a[1].l)); }
