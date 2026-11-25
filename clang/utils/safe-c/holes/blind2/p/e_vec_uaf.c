// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef float f4 __attribute__((ext_vector_type(4)));
int main(int c,char**v){ f4*p=calloc(1,16); if(!p) return 1; free(p); p->w=1; printf("%f\n",p->x); }
