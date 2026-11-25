// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
int main(void){ int z=0; int*__single e=&z; int*p=malloc(4); if(!p) return 1; int*__single _Atomic G=p; e=&z; atomic_compare_exchange_strong(&G,&e,&z); free(p); printf("%d\n",*e); }
