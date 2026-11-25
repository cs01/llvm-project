#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int c,char**v){char l[4][8]={"a"};l[c+500][0]=7;return l[0][0];}
