// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
static void f(_Atomic int*_Nullable p){ atomic_store(p,1); }
int main(void){ f(NULL); }
