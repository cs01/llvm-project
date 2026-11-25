#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int c,char**v){int*p=calloc(2,4);if(!p)return 1;return p[c+5];}
