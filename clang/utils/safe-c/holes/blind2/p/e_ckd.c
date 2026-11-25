// opts: std=c23
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdckdint.h>
int main(int c,char**v){ int*p=calloc(2,4); if(!p) return 1; printf("%d\n",ckd_add(&p[c+3],1,2)); }
