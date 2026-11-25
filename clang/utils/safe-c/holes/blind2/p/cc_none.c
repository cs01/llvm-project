// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
__attribute__((noinline)) static int g(int*x,int y){ return *x+y; }
typedef __attribute__((preserve_none)) int (*pf)(int*,int);
int main(int c,char**v){ int a=3; pf f=(pf)g; printf("%d\n",f(&a,2)); }
