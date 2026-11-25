// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ char*end; long x=strtol("12ab",&end,10); printf("%ld %s\n",x,end); }
