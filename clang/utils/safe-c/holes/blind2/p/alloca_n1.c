// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ volatile int n=-64; int sec[8]={1,2,3,4,5,6,7,8}; char*p=__builtin_alloca((size_t)n); for(int i=0;i<64;i++) p[i]=0x41; for(int i=0;i<8;i++) printf("%d ",sec[(i+c)%8]); puts(""); }
