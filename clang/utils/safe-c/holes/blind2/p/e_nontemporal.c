// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ int*p=calloc(2,4); if(!p) return 1; __builtin_nontemporal_store(c,&p[c+3]); printf("%d\n",__builtin_nontemporal_load(&p[c+5])); }
