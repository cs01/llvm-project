// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
static double f(double*_Nullable d){ return modf(3.4,d); }
int main(int c,char**v){ printf("%f\n",f(NULL)); }
