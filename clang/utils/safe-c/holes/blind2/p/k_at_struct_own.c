// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
struct v{int*_Owned _Nullable p;};
int main(void){ _Atomic struct v x; atomic_init(&x,((struct v){malloc(4)})); struct v y=atomic_load(&x),z=atomic_load(&x); free(_Take(y.p)); free(_Take(z.p)); }
