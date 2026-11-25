// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct cmd{ void(*fn)(int*); };
static void f(int*_Owned p){ free(p);} 
static const struct cmd t[]={{f}};
int main(void){ int *p=malloc(4); if(!p) return 1; t[0].fn(p); *p=3; printf("%d\n",*p);}
