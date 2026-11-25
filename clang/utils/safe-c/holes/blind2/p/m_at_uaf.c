// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
int main(void){ _Atomic int*p=malloc(4); if(!p) return 1; free(p); atomic_store(p,1); printf("x\n"); }
