#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
int main(int c,char**v){double*d=calloc(1,8);if(!d)return 1;return (int)modf(3.4*c,d+c);}
