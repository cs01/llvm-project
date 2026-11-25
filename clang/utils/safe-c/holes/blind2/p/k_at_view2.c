// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
struct _View v{int*p;};
int main(void){ int z=0; _Atomic struct v x=(struct v){&z}; int*k=malloc(4); if(!k) return 1; atomic_store(&x,((struct v){k})); free(k); struct v y=atomic_load(&x); printf("%d\n",*y.p); }
