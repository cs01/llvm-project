// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
int main(void){ _Atomic int z=0; _Atomic int*p=&z; { _Atomic int y=1; p=&y; } printf("%d\n",atomic_load(p)); }
