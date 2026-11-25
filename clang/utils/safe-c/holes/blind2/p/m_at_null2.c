// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
static int f(_Atomic int*_Nullable p){ return *p; }
int main(void){ printf("%d\n",f(NULL)); }
