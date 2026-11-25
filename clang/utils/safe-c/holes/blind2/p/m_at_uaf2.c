// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
struct o{_Atomic int*_Owned _Nullable p;};
int main(void){ struct o o={0}; o.p=malloc(4); _Atomic int*_Nullable q=o.p; free(_Take(o.p)); if(q) printf("%d\n",atomic_fetch_add(q,1)); }
