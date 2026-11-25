// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static void nop(int c){ }
typedef void nr(int) __attribute__((noreturn));
int main(int c,char**v){ int*p=malloc(4); if(!p) return 1; nr*f=(nr*)nop; if(c<9){ free(p); f(c);} *p=1; printf("%d\n",*p); }
