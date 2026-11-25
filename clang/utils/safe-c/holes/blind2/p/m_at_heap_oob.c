// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
int main(int c,char**v){ _Atomic int*p=calloc(2,4); if(!p) return 1; for(int i=0;i<3;i++) atomic_store(&p[i],i); free(p); printf("x\n"); }
