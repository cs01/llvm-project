// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef float f4 __attribute__((ext_vector_type(4)));
static void f(f4*_Nullable p){ p->w=1; }
int main(int c,char**v){ f(NULL); }
