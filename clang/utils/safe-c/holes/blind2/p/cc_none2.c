// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
__attribute__((preserve_none,noinline)) static int g(int*x,int y){ return *x+y; }
typedef int (*pf)(int*,int);
int main(int c,char**v){ int a=3; volatile pf f=(pf)g; printf("%d\n",f(&a,2)); }
