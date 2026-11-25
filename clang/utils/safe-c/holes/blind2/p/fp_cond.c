// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static void f(int*_Owned p){ free(p);} 
static void g(int*p){ *p=1;} 
typedef void (*fn)(int*);
int main(int c,char**v){ fn h=c?f:g; int *p=malloc(4); if(!p) return 1; h(p); *p=3; printf("%d\n",*p);}
