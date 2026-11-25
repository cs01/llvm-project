// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static void f(int*_Owned p){ free(p);} 
typedef void (*fn)(int*);
static fn get(void){ return f; }
int main(void){ int *p=malloc(4); if(!p) return 1; get()(p); *p=3; printf("%d\n",*p);}
