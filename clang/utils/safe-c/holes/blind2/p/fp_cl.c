// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct ops{ void(*fn)(int*); };
static void f(int*_Owned p){ free(p);} 
int main(void){ int *p=malloc(4); if(!p) return 1; ((struct ops){f}).fn(p); *p=3; printf("%d\n",*p);}
