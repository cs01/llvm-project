// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static void f(char*p){ printf("%s\n",p); }
int main(int c,char**v){ char*q=malloc(1); if(!q) return 1; *q='a'; f(q); free(q); }
