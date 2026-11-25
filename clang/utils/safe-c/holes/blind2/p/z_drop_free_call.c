// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ int*__single p=malloc(4); if(!p) return 1; safec_drop_free_(&p); *p=1; }
