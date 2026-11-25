// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
int main(void){ int z=0; int*__single _Atomic G=&z; int*p=malloc(4); if(!p) return 1; atomic_store(&G,p); free(p); int*__single q=atomic_load(&G); printf("%d\n",*q); }
