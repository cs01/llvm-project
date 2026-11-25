// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
int main(int c,char**v){ double*d=calloc(1,8); if(!d) return 1; printf("%f\n",modf(3.4*c,d+c)); }
