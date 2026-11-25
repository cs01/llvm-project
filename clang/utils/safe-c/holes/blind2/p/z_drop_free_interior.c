// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ char*p _Drop_free=malloc(8); if(!p) return 1; p++; printf("%d\n",*p); }
