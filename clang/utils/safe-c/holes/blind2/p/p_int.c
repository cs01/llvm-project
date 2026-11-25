// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct n{int*_Owned _Nullable l; int v;};
int main(void){ struct n*r=calloc(1,sizeof*r); if(!r) return 1; int*k=calloc(1,sizeof*k); if(!k) return 1; r->l=k; free(_Take(r->l)); printf("%d\n",*k); free(r); }
